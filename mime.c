#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mail.h"

static char *b64_ch =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int b64_val[256];

static void b64_init(void)
{
	int i;
	for (i = 0; i < 64; i++)
		b64_val[(unsigned char) b64_ch[i]] = i;
}

static int b64_dec(char *d, char *s)
{
	unsigned v = 0;
	v |= b64_val[(unsigned char) s[0]];
	v <<= 6;
	v |= b64_val[(unsigned char) s[1]];
	v <<= 6;
	v |= b64_val[(unsigned char) s[2]];
	v <<= 6;
	v |= b64_val[(unsigned char) s[3]];

	d[2] = v & 0xff;
	v >>= 8;
	d[1] = v & 0xff;
	v >>= 8;
	d[0] = v & 0xff;
	return 3 - (s[1] == '=') - (s[2] == '=') - (s[3] == '=');
}

static void dec_b64(struct sbuf *sb, char *s, char *e)
{
	if (!b64_val['B'])
		b64_init();
	while (s + 4 <= e) {
		while (s < e && isspace((unsigned char) *s))
			s++;
		if (s < e) {
			char dst[4];
			int n = b64_dec(dst, s);
			s += 4;
			sbuf_mem(sb, dst, n);
		}
	}
}

static int hexval(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'A' && c <= 'F')
		return 10 + c - 'A';
	if (c >= 'a' && c <= 'f')
		return 10 + c - 'a';
	return 0;
}

static void dec_qp(struct sbuf *sb, char *s, char *e)
{
	while (s < e) {
		if (*s == '=' && s[1] == '\n') {
			s += 2;
		} else if (*s == '=' && s + 2 < e) {
			sbuf_chr(sb, (hexval(s[1]) << 4) | hexval(s[2]));
			s += 3;
		} else {
			sbuf_chr(sb, *s == '_' ? ' ' : (unsigned char) *s);
			s++;
		}
	}
}

static void toutf8(char *charset, struct sbuf *dst, char *src, int len);

static void msg_hdrdec2(struct sbuf *sb, char *hdr, char *end)
{
	while (hdr < end) {
		char *q1 = hdr[0] == '=' && hdr[1] == '?' ? hdr + 1 : NULL;
		char *q2 = q1 ? memchr(q1 + 1, '?', end - q1) : NULL;
		char *q3 = q2 ? memchr(q2 + 1, '?', end - q2) : NULL;
		char *q4 = q3 ? memchr(q3 + 1, '?', end - q3) : NULL;
		if (q1 && q2 && q3 && q4 && q4[1] == '=') {
			int c = tolower((unsigned char) q2[1]);
			struct sbuf *sb2 = sbuf_make();
			char charset[128];
			int charset_len = q2 - q1 - 1;
			if (charset_len > sizeof(charset) - 1)
				charset_len = sizeof(charset) - 1;
			memcpy(charset, q1 + 1, charset_len);
			charset[charset_len] = '\0';
			if (c == 'b')
				dec_b64(sb2, q3 + 1, q4);
			else
				dec_qp(sb2, q3 + 1, q4);
			hdr = q4 + 2;
			while (isspace((unsigned char) *hdr) && hdr + 1 < end)
				hdr++;
			toutf8(charset, sb, sbuf_buf(sb2), sbuf_len(sb2));
			sbuf_free(sb2);
		} else {
			sbuf_chr(sb, (unsigned char) *hdr++);
		}
	}
}

char *msg_hdrdec(char *hdr)
{
	struct sbuf *sb;
	sb = sbuf_make();
	msg_hdrdec2(sb, hdr, strchr(hdr, '\0'));
	return sbuf_done(sb);
}

/* decoding mime messages */

#define MAXPARTS		(1 << 3)
#define BOUNDLEN		(1 << 7)

#define TYPE_TXT	0x01
#define TYPE_MPART	0x02
#define TYPE_ETC	0x04
#define ENC_B8		0x10
#define ENC_QP		0x20
#define ENC_B64		0x40

struct mime {
	int depth;
	int part[MAXPARTS];
	char bound[MAXPARTS][BOUNDLEN];
	char *src;
	char *end;
};

static void copy_till(struct mime *m, struct sbuf *dst, char *s)
{
	int len = s - m->src;
	sbuf_mem(dst, m->src, len);
	m->src += len;
}

static void read_boundary(struct mime *m, char *s, char *hdrend)
{
	char *bound = m->bound[m->depth];
	char *e;
	s = memchr(s, '=', hdrend - s);
	if (!s)
		return;
	s++;
	if (*s == '"') {
		s++;
		e = memchr(s, '"', hdrend - s);
	} else {
		e = s;
		while (e < hdrend && !isspace(*e) && *e != ';')
			e++;
	}
	if (!e)
		return;
	bound[0] = '-';
	bound[1] = '-';
	memcpy(bound + 2, s, e - s);
	bound[e - s + 2] = '\0';
	m->depth++;
}

static char *read_charset(struct mime *m, char *s, char *hdrend)
{
	if (!(s = memchr(s, '=', hdrend - s)))
		return NULL;
	if (s + 14 < hdrend && startswith(s, "=\"windows-1256\""))
		return "windows-1256";
	if (s + 12 < hdrend && startswith(s, "=\"iso-8859-6\""))
		return "iso-8859-6";
	return NULL;
}

static char *hdr_nextfield(char *s, char *e)
{
	while (s && s < e && *s != ';')
		if (*s++ == '"')
			if ((s = memchr(s, '"', e - s)))
				s++;
	return s && s + 2 < e ? s + 1 : NULL;
}

static void read_hdrs(struct mime *m, struct sbuf *dst, int *type, char **charset)
{
	char *s = m->src;
	char *e = m->end;
	*type = 0;
	*charset = NULL;
	while (s && s < e && *s != '\n') {
		char *n = memchr(s, '\n', e - s);
		while (n && n + 1 < e && n[1] != '\n' && isspace(n[1]))
			n = memchr(n + 1, '\n', e - n - 1);
		if (!n++)
			break;
		if (startswith(s, "Content-Type:")) {
			char *key = strchr(s, ':') + 1;
			char *hdrend = s + hdrlen(s, e - s);
			while (key) {
				while (key < hdrend && isspace((unsigned char) *key))
					key++;
				if (startswith(key, "text"))
					*type |= TYPE_TXT;
				if (startswith(key, "multipart"))
					*type |= TYPE_MPART;
				if (startswith(key, "charset"))
					*charset = read_charset(m, key, hdrend);
				if (startswith(key, "boundary"))
					read_boundary(m, key, hdrend);
				key = hdr_nextfield(key, hdrend);
			}
		}
		if (startswith(s, "Content-Transfer-Encoding:")) {
			char *key = strchr(s, ':') + 1;
			char *hdrend = s + hdrlen(s, e - s);
			while (key) {
				while (key < hdrend && isspace(*key))
					key++;
				if (startswith(key, "quoted-printable"))
					*type |= ENC_QP;
				if (startswith(key, "base64"))
					*type |= ENC_B64;
				key = hdr_nextfield(key, hdrend);
			}
		}
		msg_hdrdec2(dst, s, n);
		s = n;
	}
	sbuf_chr(dst, '\n');
	m->src = s + 1;
}

static int is_bound(struct mime *m, char *s)
{
	return startswith(s, m->bound[m->depth - 1]);
}

static void read_bound(struct mime *m, struct sbuf *dst)
{
	char *s = m->src;
	int len = strlen(m->bound[m->depth - 1]);
	if (s[len] == '-' && s[len + 1] == '-')
		m->depth--;
	s = memchr(s, '\n', m->end - s);
	s = s ? s + 1 : m->end;
	copy_till(m, dst, s);
}

static char *find_bound(struct mime *m)
{
	char *s = m->src;
	char *e = m->end;
	while (s < e) {
		if (is_bound(m, s))
			return s;
		if (!(s = memchr(s, '\n', e - s)))
			break;
		s++;
	}
	return e;
}

static void read_body(struct mime *m, struct sbuf *dst, int type)
{
	char *end = m->depth ? find_bound(m) : m->end;
	if (~type & TYPE_TXT) {
		copy_till(m, dst, end);
		return;
	}
	if (type & ENC_QP) {
		dec_qp(dst, m->src, end);
		m->src = end;
		return;
	}
	if (type & ENC_B64) {
		dec_b64(dst, m->src, end);
		sbuf_chr(dst, '\n');
		m->src = end;
		return;
	}
	copy_till(m, dst, end);
}

int msg_demime(char *msg, long msglen, char **mod, long *modlen)
{
	struct sbuf *dst = sbuf_make();
	struct mime m;
	m.src = msg;
	m.end = msg + msglen;
	m.depth = 0;
	while ((m.depth && m.src < m.end) || m.src == msg) {
		int type;
		char *charset;
		struct sbuf *sb = sbuf_make();
		read_hdrs(&m, sb, &type, &charset);
		read_body(&m, sb, type);
		toutf8(charset, dst, sbuf_buf(sb), sbuf_len(sb));
		sbuf_free(sb);
		if (m.depth)
			read_bound(&m, dst);
	}
	sbuf_chr(dst, '\0');
	*modlen = sbuf_len(dst) - 1;
	*mod = sbuf_done(dst);
	return 0;
}

static char *cp1256[256] = {
	[0x80] = "€", [0x81] = "پ", [0x82] = "‚", [0x83] = "ƒ",
	[0x84] = "„", [0x85] = "…", [0x86] = "†", [0x87] = "‡",
	[0x88] = "ˆ", [0x89] = "‰", [0x8A] = "ٹ", [0x8B] = "‹",
	[0x8C] = "Œ", [0x8D] = "چ", [0x8E] = "ژ", [0x8F] = "ڈ",
	[0x90] = "گ", [0x91] = "‘", [0x92] = "’", [0x93] = "“",
	[0x94] = "”", [0x95] = "•", [0x96] = "–", [0x97] = "—",
	[0x98] = "ک", [0x99] = "™", [0x9A] = "ڑ", [0x9B] = "›",
	[0x9C] = "œ", [0x9D] = "‌", [0x9E] = "‍", [0x9F] = "ں",
	[0xA1] = "،", [0xAA] = "ھ", [0xBA] = "؛", [0xBF] = "؟",
	[0xC0] = "ہ", [0xC1] = "ء", [0xC2] = "آ", [0xC3] = "أ",
	[0xC4] = "ؤ", [0xC5] = "إ", [0xC6] = "ئ", [0xC7] = "ا",
	[0xC8] = "ب", [0xC9] = "ة", [0xCA] = "ت", [0xCB] = "ث",
	[0xCC] = "ج", [0xCD] = "ح", [0xCE] = "خ", [0xCF] = "د",
	[0xD0] = "ذ", [0xD1] = "ر", [0xD2] = "ز", [0xD3] = "س",
	[0xD4] = "ش", [0xD5] = "ص", [0xD6] = "ض", [0xD7] = "×",
	[0xD8] = "ط", [0xD9] = "ظ", [0xDA] = "ع", [0xDB] = "غ",
	[0xDC] = "ـ", [0xDD] = "ف", [0xDE] = "ق", [0xDF] = "ك",
	[0xE0] = "à", [0xE1] = "ل", [0xE2] = "â", [0xE3] = "م",
	[0xE4] = "ن", [0xE5] = "ه", [0xE6] = "و", [0xE7] = "ç",
	[0xE8] = "è", [0xE9] = "é", [0xEA] = "ê", [0xEB] = "ë",
	[0xEC] = "ى", [0xED] = "ي", [0xEE] = "î", [0xEF] = "ï",
	[0xF0] = "ً", [0xF1] = "ٌ", [0xF2] = "ٍ", [0xF3] = "َ",
	[0xF4] = "ô", [0xF5] = "ُ", [0xF6] = "ِ", [0xF7] = "÷",
	[0xF8] = "ّ", [0xF9] = "ù", [0xFA] = "ْ", [0xFB] = "û",
	[0xFC] = "ü", [0xFD] = "‎", [0xFE] = "‏", [0xFF] = "ے",
};

static char *iso8859_6[256] = {
	[0xAC] = "،", [0xBB] = "؛", [0xBF] = "؟", [0xC1] = "ء",
	[0xC2] = "آ", [0xC3] = "أ", [0xC4] = "ؤ", [0xC5] = "إ",
	[0xC6] = "ئ", [0xC7] = "ا", [0xC8] = "ب", [0xC9] = "ة",
	[0xCA] = "ت", [0xCB] = "ث", [0xCC] = "ج", [0xCD] = "ح",
	[0xCE] = "خ", [0xCF] = "د", [0xD0] = "ذ", [0xD1] = "ر",
	[0xD2] = "ز", [0xD3] = "س", [0xD4] = "ش", [0xD5] = "ص",
	[0xD6] = "ض", [0xD7] = "ط", [0xD8] = "ظ", [0xD9] = "ع",
	[0xDA] = "غ", [0xE0] = "ـ", [0xE1] = "ف", [0xE2] = "ق",
	[0xE3] = "ك", [0xE4] = "ل", [0xE5] = "م", [0xE6] = "ن",
	[0xE7] = "ه", [0xE8] = "و", [0xE9] = "ى", [0xEA] = "ي",
	[0xEB] = "ً", [0xEC] = "ٌ", [0xED] = "ٍ", [0xEE] = "َ",
	[0xEF] = "ُ", [0xF0] = "ِ", [0xF1] = "ّ", [0xF2] = "ْ",
};

static void toutf8(char *charset, struct sbuf *dst, char *src, int len)
{
	int i;
	char **map = NULL;
	if (charset && !strcmp("windows-1256", charset))
		map = cp1256;
	if (charset && !strcmp("iso-8859-6", charset))
		map = iso8859_6;
	if (map) {
		for (i = 0; i < len; i++) {
			int c = (unsigned char) src[i];
			if (map[c])
				sbuf_str(dst, map[c]);
			else
				sbuf_chr(dst, c);
		}
	} else {
		sbuf_mem(dst, src, len);
	}
}
