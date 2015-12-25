#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "mail.h"
#include "regex.h"

#define EXLEN		512

static struct mbox *mbox;
static int pos;
static char kwd[EXLEN];
static char kwd_hdr[EXLEN];
static regex_t kwd_re;

static char *ex_loc(char *s, char *loc)
{
	while (*s == ':' || isspace((unsigned char) *s))
		s++;
	while (*s && !isalpha((unsigned char) *s)) {
		if (*s == '/' || *s == '?') {
			int d = *s;
			*loc++ = *s++;
			while (*s && *s != d) {
				if (*s == '\\' && s[1])
					*loc++ = *s++;
				*loc++ = *s++;
			}
		}
		if (*s)
			*loc++ = *s++;
	}
	*loc = '\0';
	return s;
}

static char *ex_cmd(char *s, char *cmd)
{
	while (isspace((unsigned char) *s))
		s++;
	while (isalpha((unsigned char) *s))
		*cmd++ = *s++;
	*cmd = '\0';
	return s;
}

static char *ex_arg(char *s, char *arg)
{
	while (isspace((unsigned char) *s))
		s++;
	if (*s == '"') {
		s++;
		while (*s && *s != '"') {
			if (*s == '\\' && s[1])
				s++;
			*arg++ = *s++;
		}
		s++;
	} else {
		while (*s && !isspace((unsigned char) *s)) {
			if (*s == '\\' && s[1])
				s++;
			*arg++ = *s++;
		}
	}
	*arg = '\0';
	return s;
}

static int ex_keyword(char *pat)
{
	struct sbuf *sb;
	char *b = pat;
	char *e = b;
	sb = sbuf_make();
	while (*++e) {
		if (*e == *pat)
			break;
		if (*e == '\\' && e[1])
			e++;
		sbuf_chr(sb, (unsigned char) *e);
	}
	if (sbuf_len(sb) && strcmp(kwd, sbuf_buf(sb))) {
		if (kwd[0])
			regfree(&kwd_re);
		snprintf(kwd, sizeof(kwd), "%s", sbuf_buf(sb));
		if (regcomp(&kwd_re, kwd, REG_EXTENDED | REG_ICASE))
			kwd[0] = '\0';
		if (kwd[0] == '^' && isalpha((unsigned char) (kwd[1])) &&
				strchr(kwd, ':')) {
			int len = strchr(kwd, ':') - kwd;
			memcpy(kwd_hdr, kwd + 1, len);
			kwd_hdr[len] = '\0';
		} else {
			strcpy(kwd_hdr, "subject:");
		}
	}
	sbuf_free(sb);
	return !kwd[0];
}

static int ex_match(int i)
{
	char *msg;
	long msglen;
	char *hdr;
	char *buf;
	int len, ret;
	if (i < 0 || i >= mbox_len(mbox))
		return 1;
	mbox_get(mbox, i, &msg, &msglen);
	hdr = msg_get(msg, msglen, kwd_hdr);
	if (!hdr)
		return 1;
	len = hdrlen(hdr, msg + msglen - hdr);
	buf = malloc(len + 1);
	memcpy(buf, hdr, len);
	buf[len] = '\0';
	ret = regexec(&kwd_re, buf, 0, NULL, 0);
	free(buf);
	return ret != 0;
}

static int ex_search(char *pat)
{
	int dir = *pat == '/' ? +1 : -1;
	int i = pos + dir;
	if (ex_keyword(pat))
		return 1;
	while (i >= 0 && i < mbox_len(mbox)) {
		if (!ex_match(i))
			return i;
		i += dir;
	}
	return pos;
}

static int ex_lineno(char *num)
{
	int n = pos;
	if (!num[0] || num[0] == '.')
		n = pos;
	if (isdigit(num[0]))
		n = atoi(num);
	if (num[0] == '$')
		n = mbox_len(mbox) - 1;
	if (num[0] == '-')
		n = pos - (num[1] ? ex_lineno(num + 1) : 1);
	if (num[0] == '+')
		n = pos + (num[1] ? ex_lineno(num + 1) : 1);
	if (num[0] == '/' && num[1])
		n = ex_search(num);
	if (num[0] == '?' && num[1])
		n = ex_search(num);
	return n;
}

static int ex_region(char *loc, int *beg, int *end)
{
	if (loc[0] == '%') {
		*beg = 0;
		*end = mbox_len(mbox);
		return 0;
	}
	if (!*loc || loc[0] == ';') {
		*beg = pos;
		*end = pos == mbox_len(mbox) ? pos : pos + 1;
		return 0;
	}
	*beg = ex_lineno(loc);
	while (*loc && *loc != ',' && *loc != ';')
		loc++;
	if (*loc == ',')
		*end = ex_lineno(++loc);
	else
		*end = *beg == mbox_len(mbox) ? *beg : *beg + 1;
	if (*beg < 0 || *beg >= mbox_len(mbox))
		return 1;
	if (*end < *beg || *end > mbox_len(mbox))
		return 1;
	return 0;
}

static int ec_rm(char *arg)
{
	mbox_set(mbox, pos, "", 0);
	return 0;
}

static int ec_cp(char *arg)
{
	char box[EXLEN];
	char *msg;
	long msz;
	int fd;
	arg = ex_arg(arg, box);
	fd = open(box, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR);
	mbox_get(mbox, pos, &msg, &msz);
	xwrite(fd, msg, msz);
	close(fd);
	return 0;
}

static int ec_mv(char *arg)
{
	ec_cp(arg);
	ec_rm("");
	return 0;
}

static int ec_hd(char *arg)
{
	char hdr[EXLEN];
	char val[EXLEN];
	char *msg, *mod;
	long msglen, modlen;
	struct sbuf *sb = sbuf_make();
	arg = ex_arg(arg, hdr);
	arg = ex_arg(arg, val);
	mbox_get(mbox, pos, &msg, &msglen);
	sbuf_printf(sb, "%s %s\n", hdr, val);
	if (msg_set(msg, msglen, &mod, &modlen, hdr, sbuf_buf(sb)))
		return 1;
	sbuf_free(sb);
	mbox_set(mbox, pos, mod, modlen);
	free(mod);
	return 0;
}

static int ec_ft(char *arg)
{
	char *msg, *mod;
	long msglen, modlen;
	mbox_get(mbox, pos, &msg, &msglen);
	if (xpipe(arg, msg, msglen, &mod, &modlen))
		return 1;
	mbox_set(mbox, pos, mod, modlen);
	free(mod);
	return 0;
}

static int ec_wr(char *arg)
{
	char box[EXLEN];
	arg = ex_arg(arg, box);
	if (box[0])
		mbox_copy(mbox, box);
	else
		mbox_save(mbox);
	return 0;
}

static int ex_exec(char *ec);

static int ec_g(char *arg)
{
	while (isspace((unsigned char) *arg))
		arg++;
	if (arg[0] != '/' || ex_keyword(arg))
		return 1;
	arg++;
	while (arg[0] && arg[0] != '/')
		arg++;
	if (kwd[0] && arg[0] == '/') {
		arg++;
		if (!ex_match(pos))
			ex_exec(arg);
		return 0;
	}
	return 1;
}

static int ex_exec(char *ec)
{
	char cmd[EXLEN];
	char *arg = ex_cmd(ec, cmd);
	if (!strcmp("rm", cmd))
		return ec_rm(arg);
	if (!strcmp("cp", cmd))
		return ec_cp(arg);
	if (!strcmp("mv", cmd))
		return ec_mv(arg);
	if (!strcmp("hd", cmd) || !strcmp("set", cmd))
		return ec_hd(arg);
	if (!strcmp("ft", cmd) || !strcmp("filt", cmd))
		return ec_ft(arg);
	if (!strcmp("w", cmd))
		return ec_wr(arg);
	if (!strcmp("g", cmd))
		return ec_g(arg);
	return 1;
}

static int ec_stat(char *ec)
{
	char *val;
	char newval[16];
	char c = ec[0];
	int i = atoi(ec + 1);
	char *msg, *mod;
	long msglen, modlen;
	if (i < 0 || i >= mbox_len(mbox))
		return 1;
	pos = i;
	mbox_get(mbox, pos, &msg, &msglen);
	val = msg_get(msg, msglen, "status:");
	if (val) {
		val += strlen("status:");
		while (isspace((unsigned char) val[0]))
			val++;
	}
	if ((!val && c == 'N') || (val && val[0] == c))
		return 0;
	sprintf(newval, "Status: %c\n", c);
	if (msg_set(msg, msglen, &mod, &modlen, "status:", newval))
		return 1;
	mbox_set(mbox, pos, mod, modlen);
	free(mod);
	return 0;
}

int ex(char *argv[])
{
	char ec[EXLEN];
	char loc[EXLEN];
	int beg, end, i;
	char *cmd;
	if (!argv[0]) {
		printf("usage: neatmail ex mbox <cmds\n");
		return 1;
	}
	mbox = mbox_open(argv[0]);
	if (!mbox) {
		fprintf(stderr, "neatmail: cannot open <%s>\n", argv[0]);
		return 1;
	}
	while (fgets(ec, sizeof(ec), stdin)) {
		char *cur = loc;
		if (isupper((unsigned char) ec[0]))
			ec_stat(ec);
		if (ec[0] != ':')
			continue;
		cmd = ex_loc(ec, loc);
		do {
			if (!ex_region(cur, &beg, &end)) {
				for (i = beg; i < end; i++) {
					pos = i;
					ex_exec(cmd);
				}
			}
			while (*cur && *cur != ';')
				cur++;
			if (*cur == ';')
				cur++;
		} while (*cur);
	}
	mbox_free(mbox);
	if (kwd[0])
		regfree(&kwd_re);
	return 0;
}
