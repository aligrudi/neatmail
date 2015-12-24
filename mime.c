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
		if (*s == '=' && s + 2 < e) {
			sbuf_chr(sb, (hexval(s[1]) << 4) | hexval(s[2]));
			s += 3;
		} else {
			sbuf_chr(sb, *s == '_' ? ' ' : (unsigned char) *s);
			s++;
		}
	}
}

char *msg_hdrdec(char *hdr)
{
	struct sbuf *sb;
	sb = sbuf_make();
	while (*hdr) {
		char *q1 = hdr[0] == '=' && hdr[1] == '?' ? hdr + 1 : NULL;
		char *q2 = q1 ? strchr(q1 + 1, '?') : NULL;
		char *q3 = q2 ? strchr(q2 + 1, '?') : NULL;
		char *q4 = q3 ? strchr(q3 + 1, '?') : NULL;
		if (q1 && q2 && q3 && q4 && q4[1] == '=') {
			int c = tolower((unsigned char) q2[1]);
			if (c == 'b')
				dec_b64(sb, q3 + 1, q4);
			else
				dec_qp(sb, q3 + 1, q4);
			hdr = q4 + 2;
			while (isspace((unsigned char) *hdr))
				hdr++;
		} else {
			sbuf_chr(sb, (unsigned char) *hdr++);
		}
	}
	return sbuf_done(sb);
}
