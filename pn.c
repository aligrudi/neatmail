#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "mail.h"

#define NHDRS		128

static char *hdrs[NHDRS];	/* included headers */
static int hdrs_n;
static char pn_buf[4096];	/* last line */
static char pn_rev;		/* push back the last line */
static int maxlen = -1;		/* body length to include */
static char *pn_path;		/* input path */
static FILE *pn_fp;		/* input file */
static long pn_pos;		/* file position */
static int pn_num;		/* message number */

static char *lnext(void)
{
	if (pn_rev) {
		pn_rev = 0;
		return pn_buf;
	}
	pn_pos += strlen(pn_buf);
	return fgets(pn_buf, sizeof(pn_buf), pn_fp);
}

static void lback(void)
{
	pn_rev = 1;
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

static int pn_one(void)
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
	beg = pn_pos;
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
	body = pn_pos;
	/* read body */
	while ((ln = lnext())) {
		if (from_(ln)) {
			lback();
			break;
		}
		if (maxlen < 0 || pn_pos - body + strlen(ln) < maxlen)
			fputs(ln, stdout);
	}
	if (pn_path && maxlen == 0)
		printf("Neatmail-Source: %d@%s %ld %ld\n", pn_num, pn_path, beg, pn_pos);
	if (maxlen >= 0 && pn_pos - body >= maxlen)
		fputs("\n", stdout);
	return 0;
}

static int pn_proc(char *path)
{
	if (path && (pn_fp = fopen(path, "r")) == NULL)
		return 1;
	if (path == NULL)
		pn_fp = stdin;
	pn_path = path;
	pn_pos = 0;
	pn_num = 0;
	pn_buf[0] = '\0';
	while (pn_one() == 0)
		pn_num++;
	if (path != NULL)
		fclose(pn_fp);
	fprintf(stderr, "%s: %d messages\n", path ? path : "stdin", pn_num);
	return 0;
}

static char *usage =
	"usage: neatmail pn [options] [imbox] <imbox >ombox\n\n"
	"options:\n"
	"   -h hdr  \tspecify headers to include\n"
	"   -H      \tinclude the default set of headers\n"
	"   -s size \tmaximum message body length to include\n"
	"   -b mbox \tmbox path\n";

int pn(char *argv[])
{
	char *path[16] = {NULL};
	int path_n = 0;
	int i;
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
			path[path_n++] = argv[i][2] ? argv[i] + 2 : argv[++i];
			continue;
		}
	}
	for (; argv[i]; i++)
		path[path_n++] = argv[i];
	for (i = 0; i < path_n; i++)
		if (pn_proc(path[i]) != 0)
			fprintf(stderr, "pn: cannot open <%s>\n", path[i]);
	if (path_n == 0)
		pn_proc(NULL);
	return 0;
}
