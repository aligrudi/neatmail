#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mail.h"

#define USERAGENT	"neatmail (git://repo.or.cz/neatmail.git)"

static void msg_new(char **msg, long *msglen);
static int msg_reply(char *msg, long msglen, char **mod, long *modlen);
static int msg_forward(char *msg, long msglen, char **mod, long *modlen);

static char *segment(char *d, char *s, int m)
{
	char *r = strchr(s, m);
	char *e = r ? r + 1 : strchr(s, '\0');
	memcpy(d, s, e - s);
	d[e - s] = '\0';
	return e;
}

static int msg_filter(char *msg, long msglen, char **mod, long *modlen, char *hdrs)
{
	struct sbuf *sb = sbuf_make();
	char *hdr = malloc(strlen(hdrs) + 1);
	char *s = msg;
	char *e = msg + msglen;
	while ((hdrs = segment(hdr, hdrs, ':')) && hdr[0]) {
		char *val = msg_get(msg, msglen, hdr);
		if (val)
			sbuf_mem(sb, val, hdrlen(val, msg + msglen - val));
	}
	free(hdr);
	while (s + 1 < e && (s[0] != '\n' || s[1] != '\n'))
		s++;
	s++;
	sbuf_mem(sb, s, e - s);
	*modlen = sbuf_len(sb);
	*mod = sbuf_done(sb);
	return 0;
}

static char *usage =
	"usage: neatmail pg [options] mbox msg\n\n"
	"options:\n"
	"   -h hdrs \tthe list of headers to include\n"
	"   -m      \tdecode mime message\n"
	"   -r      \tgenerate a reply\n"
	"   -f      \tgenerate a forward\n"
	"   -n      \tgenerate a new message\n";

int pg(char *argv[])
{
	char *mbox, *addr;
	char *msg, *mod;
	char *hdrs = NULL;
	long msglen, modlen;
	int demime = 0;
	int reply = 0;
	int forward = 0;
	int newmsg = 0;
	int i;
	for (i = 0; argv[i] && argv[i][0] == '-'; i++) {
		if (argv[i][1] == 'm')
			demime = 1;
		if (argv[i][1] == 'r')
			reply = 1;
		if (argv[i][1] == 'n')
			newmsg = 1;
		if (argv[i][1] == 'f')
			forward = 1;
		if (argv[i][1] == 'h') {
			hdrs = argv[i][2] ? argv[i] + 2 : argv[++i];
			continue;
		}
	}
	if (newmsg) {
		msg_new(&msg, &msglen);
		xwrite(1, msg, msglen);
		free(msg);
		return 0;
	}
	if (!argv[i] || !argv[i + 1]) {
		printf("%s", usage);
		return 1;
	}
	mbox = argv[i];
	addr = argv[i + 1];
	if (!mbox_ith(mbox, atoi(addr), &msg, &msglen)) {
		if (demime && !msg_demime(msg, msglen, &mod, &modlen)) {
			free(msg);
			msg = mod;
			msglen = modlen;
		}
		if (reply && !msg_reply(msg, msglen, &mod, &modlen)) {
			free(msg);
			msg = mod;
			msglen = modlen;
		}
		if (hdrs && !msg_filter(msg, msglen, &mod, &modlen, hdrs)) {
			free(msg);
			msg = mod;
			msglen = modlen;
		}
		if (forward && !msg_forward(msg, msglen, &mod, &modlen)) {
			free(msg);
			msg = mod;
			msglen = modlen;
		}
		xwrite(1, msg, msglen);
		free(msg);
	}
	return 0;
}

static void put_from_(struct sbuf *sb)
{
	char buf[128];
	time_t t;
	time(&t);
	strftime(buf, sizeof(buf), "%a %b %d %H:%M:%S %Y", localtime(&t));
	sbuf_printf(sb, "From %s %s\n",
		getenv("LOGNAME") ? getenv("LOGNAME") : "me", buf);
}

static void put_date(struct sbuf *sb)
{
	char buf[128];
	time_t t;
	time(&t);
	strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S %z", localtime(&t));
	sbuf_printf(sb, "Date: %s\n", buf);
}

static void put_id(struct sbuf *sb)
{
	char buf[128];
	char host[32] = "neatmail";
	time_t t;
	time(&t);
	strftime(buf, sizeof(buf), "%Y%d%m%H%M%S", localtime(&t));
	sbuf_printf(sb, "Message-ID: <%s.%s>\n", host, buf);
}

static void put_agent(struct sbuf *sb)
{
	sbuf_printf(sb, "User-Agent: " USERAGENT "\n");
}

static void msg_new(char **msg, long *msglen)
{
	struct sbuf *sb = sbuf_make();
	put_from_(sb);
	sbuf_printf(sb, "From: \n");
	sbuf_printf(sb, "To: \n");
	sbuf_printf(sb, "Subject: \n");
	put_id(sb);
	put_date(sb);
	put_agent(sb);
	sbuf_chr(sb, '\n');
	*msglen = sbuf_len(sb);
	*msg = sbuf_done(sb);
}

static char *hdr_val(char *hdr)
{
	hdr = strchr(hdr, ':') + 1;
	while (isspace(*hdr))
		hdr++;
	return hdr;
}

static int hdr_len(char *hdr)
{
	int l = hdrlen(hdr, 1024);
	while (l > 0 && strchr(" \r\n", (unsigned char) hdr[l - 1]))
		l--;
	return l;
}

static void put_subjreply(struct sbuf *sb, char *subj)
{
	subj = hdr_val(subj);
	sbuf_str(sb, "Subject: ");
	if (tolower(subj[0]) != 'r' || tolower(subj[1]) != 'e')
		sbuf_str(sb, "Re: ");
	sbuf_mem(sb, subj, hdr_len(subj));
	sbuf_str(sb, "\n");
}

static void put_subjfwd(struct sbuf *sb, char *subj)
{
	subj = hdr_val(subj);
	sbuf_str(sb, "Subject: ");
	sbuf_str(sb, "Fwd: ");
	sbuf_mem(sb, subj, hdr_len(subj));
	sbuf_str(sb, "\n");
}

static void put_replyto(struct sbuf *sb, char *id, char *ref)
{
	id = hdr_val(id);
	sbuf_str(sb, "In-Reply-To: ");
	sbuf_mem(sb, id, hdr_len(id));
	sbuf_str(sb, "\n");
	sbuf_str(sb, "References: ");
	if (ref) {
		ref = hdr_val(ref);
		sbuf_mem(sb, ref, hdr_len(ref));
		sbuf_str(sb, "\n\t");
	}
	sbuf_mem(sb, id, hdr_len(id));
	sbuf_str(sb, "\n");
}

static void put_reply(struct sbuf *sb, char *from, char *to, char *cc, char *rply)
{
	if (from || rply) {
		char *hdr = rply ? rply : from;
		hdr = hdr_val(hdr);
		sbuf_str(sb, "To: ");
		sbuf_mem(sb, hdr, hdr_len(hdr));
		sbuf_str(sb, "\n");
	}
	if (to || cc || (rply && from)) {
		int n = 0;
		sbuf_str(sb, "Cc: ");
		if (to) {
			to = hdr_val(to);
			if (n++)
				sbuf_str(sb, "\n\t");
			sbuf_mem(sb, to, hdr_len(to));
		}
		if (rply && from) {
			from = hdr_val(from);
			if (n++)
				sbuf_str(sb, "\n\t");
			sbuf_mem(sb, from, hdr_len(from));
		}
		if (cc) {
			cc = hdr_val(cc);
			if (n++)
				sbuf_str(sb, "\n\t");
			sbuf_mem(sb, cc, hdr_len(cc));
		}
		sbuf_str(sb, "\n");
	}
}

static void quote_body(struct sbuf *sb, char *msg, long msglen)
{
	char *from = msg_get(msg, msglen, "From:");
	char *s = msg;
	char *e = msg + msglen;
	while (s + 1 < e && (s[0] != '\n' || s[1] != '\n'))
		s++;
	s += 2;
	sbuf_chr(sb, '\n');
	if (from) {
		from = hdr_val(from);
		sbuf_mem(sb, from, hdr_len(from));
		sbuf_str(sb, " wrote:\n");
	}
	while (s < e) {
		char *r = memchr(s, '\n', e - s);
		if (!r)
			break;
		sbuf_str(sb, "> ");
		sbuf_mem(sb, s, r - s + 1);
		s = r + 1;
	}
}

static int msg_reply(char *msg, long msglen, char **mod, long *modlen)
{
	struct sbuf *sb = sbuf_make();
	char *id_hdr = msg_get(msg, msglen, "Message-ID:");
	char *ref_hdr = msg_get(msg, msglen, "References:");
	char *from_hdr = msg_get(msg, msglen, "From:");
	char *subj_hdr = msg_get(msg, msglen, "Subject:");
	char *to_hdr = msg_get(msg, msglen, "To:");
	char *cc_hdr = msg_get(msg, msglen, "CC:");
	char *rply_hdr = msg_get(msg, msglen, "Reply-To:");
	put_from_(sb);
	put_date(sb);
	sbuf_printf(sb, "From: \n");
	put_reply(sb, from_hdr, to_hdr, cc_hdr, rply_hdr);
	if (subj_hdr)
		put_subjreply(sb, subj_hdr);
	put_id(sb);
	if (id_hdr)
		put_replyto(sb, id_hdr, ref_hdr);
	put_agent(sb);
	quote_body(sb, msg, msglen);
	*modlen = sbuf_len(sb);
	*mod = sbuf_done(sb);
	return 0;
}

static int msg_forward(char *msg, long msglen, char **mod, long *modlen)
{
	struct sbuf *sb = sbuf_make();
	char *subj_hdr = msg_get(msg, msglen, "Subject:");
	put_from_(sb);
	put_date(sb);
	sbuf_printf(sb, "From: \n");
	sbuf_printf(sb, "To: \n");
	put_subjfwd(sb, subj_hdr);
	put_id(sb);
	put_agent(sb);
	sbuf_str(sb, "\n-------- Original Message --------\n");
	sbuf_mem(sb, msg, msglen);
	*modlen = sbuf_len(sb);
	*mod = sbuf_done(sb);
	return 0;
}
