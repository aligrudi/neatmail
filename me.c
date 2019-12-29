/* encode a mime message */
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define LNSZ		1024

static char out[LNSZ * 2];	/* collected characters */
static int out_n;		/* number of characters in out */
static int out_mime;		/* set if inside a mime encoded word */
static int out_mbuf;		/* buffered characters */
static int out_mlen;		/* number of buffered characters */
static int out_mhdr;		/* header type; 's' for subject 'a' for address */

static char *b64_chr =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* encode 3 bytes in base64 */
static void b64_word(char *s, unsigned num)
{
	s[3] = b64_chr[num & 0x3f];
	num >>= 6;
	s[2] = b64_chr[num & 0x3f];
	num >>= 6;
	s[1] = b64_chr[num & 0x3f];
	num >>= 6;
	s[0] = b64_chr[num & 0x3f];
}

static void q_beg(void)
{
	out[out_n++] = '=';
	out[out_n++] = '?';
	out[out_n++] = 'U';
	out[out_n++] = 'T';
	out[out_n++] = 'F';
	out[out_n++] = '-';
	out[out_n++] = '8';
	out[out_n++] = '?';
	out[out_n++] = 'B';
	out[out_n++] = '?';
	out_mime = 1;
}

static void q_put(int c)
{
	out_mlen++;
	out_mbuf = (out_mbuf << 8) | c;
	if (out_mlen == 3) {
		b64_word(out + out_n, out_mbuf);
		out_n += 4;
		out_mlen = 0;
		out_mbuf = 0;
	}
}

static void q_end(void)
{
	int i;
	for (i = 0; out_mlen; i++)
		q_put(0);
	while (--i >= 0)
		out[out_n - i - 1] = '=';
	out_mime = 0;
	out[out_n++] = '?';
	out[out_n++] = '=';
}

static void out_flush(void)
{
	if (out_mime)
		q_end();
	out[out_n] = '\0';
	fputs(out, stdout);
	out_n = 0;
}

static void out_chr(int c)
{
	if (out_mime) {
		if (c == '\r' || c == '\n') {
			q_end();
		} else if (out_mhdr == 'a' && c != ' ' && ~c & 0x80) {
			q_end();
		} else if (out_n > 65 && (c & 0xc0) != 0x80) {
			q_end();
			out_flush();
			out[out_n++] = '\n';
			out[out_n++] = ' ';
			q_beg();
		}
	}
	if (c == '\n' || out_n > 75) {
		out[out_n++] = c;
		out_flush();
	} else {
		if (out_mhdr && (c & 0x80) && !out_mime)
			q_beg();
		if (out_mime)
			q_put(c);
		else
			out[out_n++] = c;
	}
}

static int startswith(char *r, char *s)
{
	while (*s)
		if (tolower((unsigned char) *s++) != tolower((unsigned char) *r++))
			return 0;
	return 1;
}

int me(char *argv[])
{
	char ln[LNSZ];
	while (fgets(ln, sizeof(ln), stdin)) {
		char *s = ln;
		if (ln[0] != ' ' && ln[0] != '\t')
			out_mhdr = 0;
		if (startswith(ln, "from:") || startswith(ln, "to:") ||
				startswith(ln, "cc:") || startswith(ln, "bcc:"))
			out_mhdr = 'a';
		if (startswith(ln, "subject:"))
			out_mhdr = 's';
		while (*s)
			out_chr((unsigned char) *s++);
		if (ln[0] == '\n' || ln[0] == '\r')
			break;
	}
	while (fgets(ln, sizeof(ln), stdin))
		fputs(ln, stdout);
	return 0;
}
