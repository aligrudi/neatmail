#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "mail.h"

static char *hdrnext(char *s, char *e)
{
	char *r;
	while (s + 1 < e && s[0] != '\n' && (s[0] != '\r' || s[1] != '\n')) {
		if (!(r = memchr(s, '\n', e - s - 1)))
			return NULL;
		if (isalpha((unsigned char) r[1]))
			return r + 1;
		s = r + 1;
	}
	return NULL;
}

/* return 1 if r starts with s */
int startswith(char *r, char *s)
{
	while (*s)
		if (tolower((unsigned char) *s++) != tolower((unsigned char) *r++))
			return 0;
	return 1;
}

char *msg_get(char *msg, long msz, char *hdr)
{
	char *s = msg;
	char *e = msg + msz;
	while (s && s < e) {
		if (startswith(s, hdr))
			return s;
		s = hdrnext(s, e);
	}
	return NULL;
}

int msg_set(char *msg, long msz, char **mod, long *modlen, char *hdr, char *val)
{
	char *h = msg_get(msg, msz, hdr);
	int off;
	int hlen = h ? hdrlen(h, msg + msz - h) : 0;
	int vlen = strlen(val);
	if (!h) {
		h = msg;
		while (hdrnext(h, msg + msz))
			h = hdrnext(h, msg + msz);
		h = h + hdrlen(h, msg + msz - h);
	}
	off = h - msg;
	*modlen = msz - hlen + vlen;
	*mod = malloc(*modlen + 1);
	if (!*mod)
		return 1;
	memcpy(*mod, msg, off);
	memcpy(*mod + off, val, vlen);
	memcpy(*mod + off + vlen, msg + off + hlen, msz - off - hlen + 1);
	(*mod)[*modlen] = '\0';
	return 0;
}

int hdrlen(char *hdr, long len)
{
	char *r, *s = hdr;
	do {
		if (!(r = strchr(s, '\n')))
			return strchr(s, '\0') - hdr;
		s = r + 1;
	} while (s[0] && (s[0] == ' ' || s[0] == '\t'));
	return s - hdr;
}
