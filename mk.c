#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mail.h"

#define LEN(a)		(sizeof(a) / sizeof((a)[0]))

static int uc_len(char *s)
{
	int c = (unsigned char) s[0];
	if (~c & 0x80)		/* ASCII */
		return c > 0;
	if (~c & 0x40)		/* invalid UTF-8 */
		return 1;
	if (~c & 0x20)
		return 2;
	if (~c & 0x10)
		return 3;
	if (~c & 0x08)
		return 4;
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

static int msg_stat(char *msg, long msz, int pos, int def)
{
	char *val = msg_get(msg, msz, "status:");
	if (!val)
		return def;
	val += strlen("status:");
	while (val + pos < msg + msz && isspace((unsigned char) val[0]))
		val++;
	return isalpha((unsigned char) val[pos]) ? val[pos] : def;
}

static int datedate(char *s);

static char *mk_field(char *msg, long msz, char *hdr, int wid)
{
	char tbuf[128];
	struct sbuf *dst;
	int dst_wid;
	char *val, *val0, *end;
	val0 = msg_dec(msg, msz, hdr[0] == '~' ? hdr + 1 : hdr);
	if (val0) {
		val = val0 + strlen(hdr) - (hdr[0] == '~');
	} else {
		val0 = malloc(1);
		val0[0] = '\0';
		val = val0;
	}
	end = strchr(val, '\0');
	dst = sbuf_make();
	while (val < end && isspace((unsigned char) *val))
		val++;
	dst_wid = 0;
	if (!strcmp("~subject:", hdr)) {
		while (startswith(val, "re:") || startswith(val, "fwd:")) {
			sbuf_chr(dst, '+');
			dst_wid++;
			val = strchr(val, ':') + 1;
			while (val < end && isspace((unsigned char) *val))
				val++;
		}
	}
	if (!strcmp("~date:", hdr)) {
		time_t ts = datedate(val);
		strftime(tbuf, sizeof(tbuf), "%d %b %Y %H:%M:%S", localtime(&ts));
		val = tbuf;
		end = strchr(tbuf, '\0');
	}
	if (!strcmp("~size:", hdr)) {
		char fmt[16];
		sprintf(fmt, "%%%dd", wid);
		snprintf(tbuf, sizeof(tbuf), fmt, msz);
		val = tbuf;
		end = strchr(tbuf, '\0');
	}
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

static char *mk_msgid(int idx, char *box, int flg1, int flg2, int idxwid, int boxwid)
{
	static char num[32];
	char fmt[32], fmtbox[32], numbox[32] = "";
	sprintf(fmtbox, "%c%%-%ds", box ? '@' : ' ', boxwid);
	if (boxwid > 0)
		snprintf(numbox, sizeof(numbox), fmtbox, box ? box : "");
	numbox[boxwid + 1] = '\0';
	sprintf(fmt, "%c%c%%0%dd%%s", flg1, flg2, idxwid);
	snprintf(num, sizeof(num), fmt, idx, numbox);
	return num;
}

static void mk_sum(struct mbox *mbox)
{
	int stats[128] = {0};
	char *msg;
	long msz;
	int i, st;
	for (i = 0; i < mbox_len(mbox); i++) {
		mbox_get(mbox, i, &msg, &msz);
		st = msg_stat(msg, msz, 0, 'N');
		if (st >= 'A' && st <= 'Z')
			stats[st - 'A']++;
	}
	for (i = 0; i < LEN(stats); i++)
		if (stats[i])
			fprintf(stderr, "%c%04d ", 'A' + i, stats[i]);
	fprintf(stderr, "\n");
}

static char *segment(char *d, char *s, int m)
{
	char *r = strchr(s, m);
	char *e = r ? r + 1 : strchr(s, '\0');
	memcpy(d, s, e - s);
	d[e - s] = '\0';
	return e;
}

static char *usage =
	"usage: neatmail mk [options] [mbox]\n\n"
	"options:\n"
	"   -b path \tmbox path\n"
	"   -0 fmt  \tmessage first line format (e.g., 20from:40subject:)\n"
	"   -1 fmt  \tmessage second line format\n"
	"   -sd     \tsort by receiving date\n"
	"   -st     \tsort by threads\n"
	"   -r      \tprint a summary of status flags\n"
	"   -f n    \tthe first message to list\n"
	"   -n n    \tmessage index field width\n"
	"   -m n    \tmessage file field width\n";

static int sort_mails(struct mbox *mbox, int *mids, int *levs);

int mk(char *argv[])
{
	int *mids, *levs;
	struct mbox *mbox;
	char *ln[4] = {"18from:40~subject:"};
	int i, j, k;
	int beg = 0;
	int sort = 0;
	int sum = 0;
	int idxwid = 4;
	int boxwid = 0;
	char *path[16] = {NULL};
	int path_n = 0;
	for (i = 0; argv[i] && argv[i][0] == '-'; i++) {
		if (argv[i][1] == 'f') {
			beg = atoi(argv[i][2] ? argv[i] + 2 : argv[++i]);
			continue;
		}
		if (argv[i][1] == 'r') {
			sum = 1;
			continue;
		}
		if (argv[i][1] == 's') {
			char t = (argv[i][2] ? argv[i] + 2 : argv[++i])[0];
			sort = t == 't' ? 2 : 1;
			continue;
		}
		if (argv[i][1] == 'b') {
			path[path_n++] = argv[i][2] ? argv[i] + 2 : argv[++i];
			continue;
		}
		if (argv[i][1] == '0' || argv[i][1] == '1') {
			int idx = argv[i][1] - '0';
			ln[idx] = argv[i][2] ? argv[i] + 2 : argv[++i];
			continue;
		}
		if (argv[i][1] == 'n') {
			idxwid = atoi(argv[i][2] ? argv[i] + 2 : argv[++i]);
			continue;
		}
		if (argv[i][1] == 'm') {
			boxwid = atoi(argv[i][2] ? argv[i] + 2 : argv[++i]);
			continue;
		}
	}
	if (!path[0] && !argv[i]) {
		printf("%s", usage);
		return 1;
	}
	for (; argv[i]; i++)
		path[path_n++] = argv[i];
	mbox = mbox_open(path);
	if (!mbox) {
		fprintf(stderr, "neatmail: cannot open <%s>\n", path[0]);
		return 1;
	}
	mids = malloc(mbox_len(mbox) * sizeof(mids[0]));
	levs = malloc(mbox_len(mbox) * sizeof(levs[0]));
	for (i = 0; i < mbox_len(mbox); i++)
		mids[i] = i;
	for (i = 0; i < mbox_len(mbox); i++)
		levs[i] = 0;
	if (sort)
		sort_mails(mbox, mids, sort == 2 ? levs : NULL);
	for (i = beg; i < mbox_len(mbox); i++) {
		char *msg;
		long msz;
		int idx;
		int tag = mbox_pos(mbox, mids[i], &idx);
		mbox_get(mbox, mids[i], &msg, &msz);
		printf("%s", mk_msgid(idx, tag > 0 ? path[tag] : NULL,
				msg_stat(msg, msz, 0, 'N'),
				msg_stat(msg, msz, 1, '0'),
				idxwid, boxwid));
		for (j = 0; ln[j]; j++) {
			char *cln = ln[j];
			char *tok = malloc(strlen(ln[j]) + 1);
			if (j)
				printf("\n");
			while ((cln = segment(tok, cln, ':')) && tok[0]) {
				char *fmt = tok;
				char *hdr = tok;
				char *val;
				int wid = atoi(fmt);
				while (isdigit((unsigned char) *hdr))
					hdr++;
				printf("\t");
				if (!strcmp("~subject:", hdr)) {
					for (k = 0; k < levs[i]; k++)
						if (wid < 0 || k + 1 < wid)
							printf(" ");
					if (wid > 0)
						wid = wid > k ? wid - k : 1;
				}
				val = mk_field(msg, msz, hdr, wid);
				printf("[%s]", val);
				free(val);
			}
			free(tok);
		}
		printf("\n");
	}
	free(mids);
	free(levs);
	if (sum)
		mk_sum(mbox);
	mbox_free(mbox);
	return 0;
}

/* sorting messages */

struct msg {
	char *msg;
	long msglen;
	char *id;	/* message-id header value */
	int id_len;	/* message-id length */
	char *rply;	/* reply-to header value */
	int rply_len;	/* reply-to length */
	int date;	/* message receiving date */
	int depth;	/* depth of message in the thread */
	int oidx;	/* the original index of the message */
	struct msg *parent;
	struct msg *head;
	struct msg *tail;
	struct msg *next;
};

static int id_cmp(char *i1, int l1, char *i2, int l2)
{
	if (l1 != l2)
		return l2 - l1;
	return strncmp(i1, i2, l1);
}

static int msgcmp_id(void *v1, void *v2)
{
	struct msg *t1 = *(struct msg **) v1;
	struct msg *t2 = *(struct msg **) v2;
	return id_cmp(t1->id, t1->id_len, t2->id, t2->id_len);
}

static int msgcmp_date(void *v1, void *v2)
{
	struct msg *t1 = *(struct msg **) v1;
	struct msg *t2 = *(struct msg **) v2;
	return t1->date == t2->date ? t1->oidx - t2->oidx : t1->date - t2->date;
}

static char *months[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static char *readtok(char *s, char *d)
{
	while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '(' || *s == ':') {
		if (*s == '(')
			while (*s && *s != ')')
				s++;
		if (*s)
			s++;
	}
	while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '(' && *s != ':')
		*d++ = *s++;
	*d = '\0';
	return s;
}

static int fromdate(char *s)
{
	char tok[128];
	int year, mon, day, hour, min, sec = 0;
	int i;
	/* parsing "From ali Tue Apr 16 20:18:40 2013" */
	s = readtok(s, tok);		/* From */
	s = readtok(s, tok);		/* username */
	s = readtok(s, tok);		/* day of week */
	s = readtok(s, tok);		/* month name */
	mon = 0;
	for (i = 0; i < LEN(months); i++)
		if (!strcmp(months[i], tok))
			mon = i;
	s = readtok(s, tok);		/* day of month */
	day = atoi(tok);
	s = readtok(s, tok);		/* hour */
	hour = atoi(tok);
	s = readtok(s, tok);		/* minute */
	min = atoi(tok);
	s = readtok(s, tok);		/* seconds (optional) */
	if (sec < 60) {
		sec = atoi(tok);
		s = readtok(s, tok);	/* year */
	}
	year = atoi(tok);
	return ((year - 1970) * 400 + mon * 31 + day) * 24 * 3600 +
		hour * 3600 + min * 60 + sec;
}

static int datedate(char *s)
{
	char tok[128];
	struct tm tm = {0};
	int ts, tz, i;
	/* parsing "Fri, 25 Dec 2015 20:26:18 +0100" */
	s = readtok(s, tok);		/* day of week (optional) */
	if (strchr(tok, ','))
		s = readtok(s, tok);	/* day of month */
	tm.tm_mday = atoi(tok);
	s = readtok(s, tok);		/* month name */
	for (i = 0; i < LEN(months); i++)
		if (!strcmp(months[i], tok))
			tm.tm_mon = i;
	s = readtok(s, tok);		/* year */
	tm.tm_year = atoi(tok) - 1900;
	s = readtok(s, tok);		/* hour */
	tm.tm_hour = atoi(tok);
	s = readtok(s, tok);		/* minute */
	tm.tm_min = atoi(tok);
	s = readtok(s, tok);		/* seconds (optional) */
	if (tok[0] != '+' && tok[0] != '-') {
		tm.tm_sec = atoi(tok);
		s = readtok(s, tok);	/* time-zone */
	}
	tz = atoi(tok);
	ts = mktime(&tm);
	if (tz >= 0)
		ts -= (tz / 100) * 3600 + (tz % 100) * 60;
	else
		ts += (-tz / 100) * 3600 + (-tz % 100) * 60;
	return ts;
}

static struct msg *msg_byid(struct msg **msgs, int n, char *id, int len)
{
	int l = 0;
	int h = n;
	while (l < h) {
		int m = (l + h) / 2;
		int d = id_cmp(id, len, msgs[m]->id, msgs[m]->id_len);
		if (!d)
			return msgs[m];
		if (d < 0)
			h = m;
		else
			l = m + 1;
	}
	return NULL;
}

static void msgs_tree(struct msg **all, struct msg **sorted_id, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		struct msg *msg = all[i];
		struct msg *dad;
		if (!msg->rply)
			continue;
		dad = msg_byid(sorted_id, n, msg->rply, msg->rply_len);
		if (dad && dad->date < msg->date) {
			msg->parent = dad;
			msg->depth = dad->depth + 1;
			if (!msg->parent->head)
				msg->parent->head = msg;
			else
				msg->parent->tail->next = msg;
			msg->parent->tail = msg;
		}
	}
}

static void msg_init(struct msg *msg)
{
	char *id_hdr = msg_get(msg->msg, msg->msglen, "Message-ID:");
	char *rply_hdr = msg_get(msg->msg, msg->msglen, "In-Reply-To:");
	char *date_hdr = msg_get(msg->msg, msg->msglen, "Date:");
	char *end = msg->msg + msg->msglen;
	if (id_hdr) {
		int len = hdrlen(id_hdr, end - id_hdr);
		char *beg = memchr(id_hdr, '<', len);
		char *end = beg ? memchr(id_hdr, '>', len) : NULL;
		if (beg && end) {
			while (*beg == '<')
				beg++;
			msg->id = beg;
			msg->id_len = end - beg;
		}
	}
	if (rply_hdr) {
		int len = hdrlen(rply_hdr, end - rply_hdr);
		char *beg = memchr(rply_hdr, '<', len);
		char *end = beg ? memchr(rply_hdr, '>', len) : NULL;
		if (beg && end) {
			while (*beg == '<')
				beg++;
			msg->rply = beg;
			msg->rply_len = end - beg;
		}
	}
	msg->date = date_hdr ? datedate(date_hdr + 5) : fromdate(msg->msg);
}

static struct msg **put_msg(struct msg **sorted, struct msg *msg)
{
	struct msg *cur = msg->head;
	*sorted++ = msg;
	while (cur) {
		sorted = put_msg(sorted, cur);
		cur = cur->next;
	}
	return sorted;
}

static void msgs_sort(struct msg **sorted, struct msg **msgs, int n)
{
	int i;
	for (i = 0; i < n; i++)
		if (!msgs[i]->parent)
			sorted = put_msg(sorted, msgs[i]);
}

static int sort_mails(struct mbox *mbox, int *mids, int *levs)
{
	int n = mbox_len(mbox);
	struct msg *msgs = malloc(n * sizeof(*msgs));
	struct msg **sorted_date = malloc(n * sizeof(*sorted_date));
	struct msg **sorted_id = malloc(n * sizeof(*sorted_id));
	struct msg **sorted = malloc(n * sizeof(*sorted));
	int i;
	if (!msgs || !sorted_date || !sorted_id) {
		free(msgs);
		free(sorted_date);
		free(sorted_id);
		free(sorted);
		return 1;
	}
	memset(msgs, 0, n * sizeof(*msgs));
	for (i = 0; i < n; i++) {
		struct msg *msg = &msgs[i];
		msg->oidx = i;
		mbox_get(mbox, i, &msg->msg, &msg->msglen);
		sorted_id[i] = msg;
		sorted_date[i] = msg;
		msg_init(msg);
	}
	qsort(sorted_date, n, sizeof(*sorted_date), (void *) msgcmp_date);
	qsort(sorted_id, n, sizeof(*sorted_id), (void *) msgcmp_id);
	if (levs)
		msgs_tree(sorted_date, sorted_id, n);
	msgs_sort(sorted, sorted_date, n);
	for (i = 0; i < n; i++)
		mids[i] = sorted[i]->oidx;
	if (levs)
		for (i = 0; i < n; i++)
			levs[i] = sorted[i]->depth;
	free(msgs);
	free(sorted_date);
	free(sorted_id);
	free(sorted);
	return 0;
}
