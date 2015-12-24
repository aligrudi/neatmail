/*
 * neatmail mail client
 *
 * Copyright (C) 2009-2015 Ali Gholami Rudi <ali at rudi dot ir>
 *
 * This program is released under the Modified BSD license.
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

static int pg(char *argv[])
{
	char *mbox = argv[0];
	char *addr = argv[1];
	char *msg;
	long msz;
	if (!mbox_ith(mbox, atoi(addr), &msg, &msz)) {
		xwrite(1, msg, msz);
		free(msg);
	}
	return 0;
}

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

int main(int argc, char *argv[])
{
	signal(SIGPIPE, SIG_IGN);
	if (argv[1] && !strcmp("mk", argv[1]))
		mk(argv + 2);
	if (argv[1] && !strcmp("ex", argv[1]))
		ex(argv + 2);
	if (argv[1] && !strcmp("pg", argv[1]))
		pg(argv + 2);
	if (argv[1] && !strcmp("ns", argv[1]))
		ns(argv + 2);
	return 0;
}
