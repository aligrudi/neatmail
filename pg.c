#include <stdio.h>
#include <stdlib.h>
#include "mail.h"

int pg(char *argv[])
{
	char *mbox, *addr;
	char *msg, *mod;
	long msglen, modlen;
	int demime = 0;
	int i;
	for (i = 0; argv[i] && argv[i][0] == '-'; i++) {
		if (argv[i][1] == 'm')
			demime = 1;
	}
	if (!argv[i] || !argv[i + 1])
		return 1;
	mbox = argv[i];
	addr = argv[i + 1];
	if (!mbox_ith(mbox, atoi(addr), &msg, &msglen)) {
		if (demime && !msg_demime(msg, msglen, &mod, &modlen)) {
			free(msg);
			msg = mod;
			msglen = modlen;
		}
		xwrite(1, msg, msglen);
		free(msg);
	}
	return 0;
}
