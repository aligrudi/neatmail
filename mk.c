#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mail.h"

static int uc_len(char *s)
{
	int c = (unsigned char) s[0];
	if (~c & 0x80)
		return c > 0;
	if (~c & 0x20)
		return 2;
	if (~c & 0x10)
		return 3;
	if (~c & 0x80)
		return 4;
	if (~c & 0x40)
		return 5;
	if (~c & 0x20)
		return 6;
	return 1;
}

static int uc_wid(char *s)
{
	return 1;
}

static char *msg_dec(char *msg, long msz, char *hdr)
{
	char *val = msg_get(msg, msz, hdr);
	char *buf, *ret;
	int val_len;
	if (!val)
		return NULL;
	val_len = hdrlen(val, msg + msz - val) - 1;
	buf = malloc(val_len + 1);
	memcpy(buf, val, val_len);
	buf[val_len] = '\0';
	ret = msg_hdrdec(buf);
	free(buf);
	return ret;
}

static int msg_stat(char *msg, long msz)
{
	char *val = msg_get(msg, msz, "status:");
	if (!val)
		return 'N';
	val += strlen("status:");
	while (isspace((unsigned char) val[0]))
		val++;
	return val[0];
}

static char *fieldformat(char *msg, long msz, char *hdr, int wid)
{
	struct sbuf *dst;
	int dst_wid;
	char *val = msg_dec(msg, msz, hdr);
	char *val0, *end;
	if (!val) {
		val = malloc(1);
		val[0] = '\0';
	}
	val0 = val;
	end = strchr(val, '\0');
	dst = sbuf_make();
	val += strlen(hdr);
	while (val < end && isspace((unsigned char) *val))
		val++;
	dst_wid = 0;
	while (val < end && (wid <= 0 || dst_wid < wid)) {
		int l = uc_len(val);
		if (l == 1) {
			int c = (unsigned char) *val;
			sbuf_chr(dst, isblank(c) || !isprint(c) ? ' ' : c);
		} else {
			sbuf_mem(dst, val, l);
		}
		dst_wid += uc_wid(val);
		val += l;
	}
	if (wid > 0)
		while (dst_wid++ < wid)
			sbuf_chr(dst, ' ');
	free(val0);
	return sbuf_done(dst);
}

static char **segs_make(char *s, int d, int inc)
{
	char **segs;
	char *r = s;
	int n = 1;
	int i;
	while (strchr(r, d)) {
		n++;
		r = strchr(r, d) + 1;
	}
	segs = malloc((n + 1) * sizeof(segs[0]));
	segs[n] = NULL;
	for (i = 0; i < n; i++) {
		r = strchr(s, i < n - 1 ? d : '\0');
		segs[i] = malloc(r - s + 2);
		memcpy(segs[i], s, r - s + inc);
		segs[i][r - s + inc] = '\0';
		s = r + 1;
	}
	return segs;
}

static void segs_free(char **segs)
{
	int i;
	for (i = 0; segs[i]; i++)
		free(segs[i]);
	free(segs);
}

int mk(char *argv[])
{
	struct mbox *mbox;
	char **ln[4];
	int i, j, k;
	int first = 0;
	for (i = 0; argv[i] && argv[i][0] == '-'; i++) {
		if (argv[i][1] == 'f') {
			first = atoi(argv[i][2] ? argv[i] + 2 : argv[++i]);
			continue;
		}
		if (argv[i][1] == '0' || argv[i][1] == '1') {
			int idx = argv[i][1] - '0';
			char *fmt = argv[i][2] ? argv[i] + 2 : argv[++i];
			ln[idx] = segs_make(fmt, ':', 1);
			continue;
		}
	}
	if (!argv[i])
		return 1;
	mbox = mbox_open(argv[i]);
	for (i = first; i < mbox_len(mbox); i++) {
		char *msg;
		long msz;
		mbox_get(mbox, i, &msg, &msz);
		printf("%c%04d", msg_stat(msg, msz), i);
		for (j = 0; ln[j]; j++) {
			if (j)
				printf("\n");
			for (k = 0; ln[j][k] && ln[j][k][0]; k++) {
				char *fmt = ln[j][k];
				char *hdr = fmt;
				char *val;
				while (isdigit((unsigned char) *hdr))
					hdr++;
				val = fieldformat(msg, msz, hdr, atoi(fmt));
				printf("\t%s", val);
				free(val);
			}
		}
		printf("\n");
	}
	mbox_free(mbox);
	for (i = 0; ln[i]; i++)
		segs_free(ln[i]);
	return 0;
}
