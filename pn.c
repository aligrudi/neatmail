#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "mail.h"

#define NHDRS		128

static char *hdrs[NHDRS];
static int hdrs_n;
static char lnext_buf[4096];
static char lnext_back;
static int maxlen = -1;		/* body length to include */
static long pos;		/* input file offset */
static char *path = NULL;	/* mbox address for neatmail-source header */

static char *lnext(void)
{
	if (lnext_back) {
		lnext_back = 0;
		return lnext_buf;
	}
	pos += strlen(lnext_buf);
	return fgets(lnext_buf, sizeof(lnext_buf), stdin);
}

static void lback(void)
{
	lnext_back = 1;
}

static int hdrs_add(char *hdr)
{
	if (hdrs_n < NHDRS)
		hdrs[hdrs_n++] = hdr;
	return 0;
}

static int hdrs_find(char *ln)
{
	int i;
	for (i = 0; i < hdrs_n; i++)
		if (startswith(ln, hdrs[i]))
			return i;
	return -1;
}

static int from_(char *s)
{
	return s && s[0] == 'F' && s[1] == 'r' && s[2] == 'o' &&
			s[3] == 'm' && s[4] == ' ';
}

static int msg_read(int n)
{
	char *ln;
	int hdrout = 0;
	long beg, body;
	while ((ln = lnext())) {
		fputs(ln, stdout);
		if (ln[0] != '\n')
			break;
	}
	if (!ln)
		return -1;
	beg = pos;
	/* read headers */
	while ((ln = lnext())) {
		if (from_(ln) || ln[0] == '\n') {
			lback();
			break;
		}
		if (ln[0] != ' ' && ln[0] != '\t')
			hdrout = hdrs_find(ln) >= 0;
		if (hdrout)
			fputs(ln, stdout);
	}
	body = pos;
	/* read body */
	while ((ln = lnext())) {
		if (from_(ln)) {
			lback();
			break;
		}
		if (maxlen < 0 || pos - body + strlen(ln) < maxlen)
			fputs(ln, stdout);
	}
	if (path && maxlen == 0)
		printf("Neatmail-Source: %d@%s %ld %ld\n", n, path, beg, pos);
	if (maxlen >= 0 && pos - body >= maxlen)
		fputs("\n", stdout);
	return 0;
}

static char *usage =
	"usage: neatmail pn [options] <imbox >ombox\n\n"
	"options:\n"
	"   -h hdr  \tspecify headers to include\n"
	"   -H      \tinclude the default set of headers\n"
	"   -s size \tmaximum message body length to include\n"
	"   -b mbox \tinclude a neatmail-source: header\n";

int pn(char *argv[])
{
	int i;
	int n = 0;
	if (!argv[0]) {
		printf("%s", usage);
		return 1;
	}
	for (i = 0; argv[i] && argv[i][0] == '-'; i++) {
		if (argv[i][1] == 'h') {
			hdrs_add(argv[i][2] ? argv[i] + 2 : argv[++i]);
			continue;
		}
		if (argv[i][1] == 'H') {
			hdrs_add("From:");
			hdrs_add("To:");
			hdrs_add("Cc:");
			hdrs_add("Bcc:");
			hdrs_add("Subject:");
			hdrs_add("Date:");
			hdrs_add("Message-ID:");
			hdrs_add("Reply-To:");
			hdrs_add("In-Reply-To:");
			hdrs_add("References:");
			hdrs_add("MIME-Version:");
			hdrs_add("Content-Type:");
			hdrs_add("Content-Transfer-Encoding:");
			hdrs_add("User-Agent:");
			hdrs_add("X-Mailer:");
			hdrs_add("Organization:");
			hdrs_add("Status:");
			hdrs_add("Neatmail");
			continue;
		}
		if (argv[i][1] == 's') {
			maxlen = atoi(argv[i][2] ? argv[i] + 2 : argv[++i]);
			continue;
		}
		if (argv[i][1] == 'b') {
			path = argv[i][2] ? argv[i] + 2 : argv[++i];
			continue;
		}
	}
	while (!msg_read(n))
		n++;
	fprintf(stderr, "Messages: %d\n", n);
	return 0;
}
