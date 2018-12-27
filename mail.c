/*
 * NEATMAIL MAIL CLIENT
 *
 * Copyright (C) 2009-2018 Ali Gholami Rudi <ali at rudi dot ir>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "mail.h"

static int has_mail(char *path)
{
	struct stat st;
	if (stat(path, &st) == -1)
		return 0;
	return st.st_mtime > st.st_atime;
}

static int ns(char *argv[])
{
	int i;
	for (i = 0; argv[i]; i++)
		if (has_mail(argv[i]))
			printf("%s\n", argv[i]);
	return 0;
}

static char *usage =
	"usage: neatmail command [options]\n\n"
	"commands:\n"
	"   ex  \texecute commands on an mbox\n"
	"   mk  \tlist the messages in an mbox\n"
	"   pg  \tpage a message of an mbox\n"
	"   ns  \tcheck mboxes for new mails\n";

int main(int argc, char *argv[])
{
	signal(SIGPIPE, SIG_IGN);
	if (!argv[1])
		printf("%s", usage);
	if (argv[1] && !strcmp("mk", argv[1]))
		return mk(argv + 2);
	if (argv[1] && !strcmp("ex", argv[1]))
		return ex(argv + 2);
	if (argv[1] && !strcmp("pg", argv[1]))
		return pg(argv + 2);
	if (argv[1] && !strcmp("ns", argv[1]))
		return ns(argv + 2);
	return 0;
}
