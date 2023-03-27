#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#include "mail.h"

#define MBOX_N	16		/* number of mboxes */

struct mbox {
	char *boxpath[MBOX_N];	/* mbox paths */
	char *boxbuf[MBOX_N];	/* mbox bufferes */
	long boxlen[MBOX_N];	/* mbox buf lengths */
	int boxcnt[MBOX_N];	/* number mbox messages */
	int boxbeg[MBOX_N];	/* mbox's first message */
	int boxend[MBOX_N];	/* mbox's last message */
	int cnt;		/* number of mboxes */
	char **msg;		/* messages */
	long *msglen;		/* message lengths */
	char **mod;		/* modified messages */
	long *modlen;		/* modified message lengths */
	int msgmax;		/* size of msg arrays */
	int msgcnt;		/* number of messages */
};

static void set_atime(char *filename)
{
	struct stat st;
	struct utimbuf times;
	stat(filename, &st);
	times.actime = time(NULL);
	times.modtime = st.st_mtime;
	utime(filename, &times);
}

static char *mbox_from_(char *s, char *e)
{
	char *r;
	while (s && s + 6 < e) {
		if (s[0] == 'F' && s[1] == 'r' && s[2] == 'o' &&
				s[3] == 'm' && s[4] == ' ')
			return s;
		r = memchr(s, '\n', e - s);
		s = r && r + 7 < e ? r + 1 : NULL;
	}
	return NULL;
}

int mbox_get(struct mbox *mbox, int i, char **msg, long *msglen)
{
	if (i < 0 || i >= mbox->msgcnt)
		return 1;
	if (mbox->mod[i]) {
		*msg = mbox->mod[i];
		*msglen = mbox->modlen[i];
	} else {
		*msg = mbox->msg[i];
		*msglen = mbox->msglen[i];
	}
	return 0;
}

int mbox_set(struct mbox *mbox, int i, char *msg, long msz)
{
	if (i < 0 || i >= mbox->msgcnt)
		return 1;
	free(mbox->mod[i]);
	mbox->mod[i] = malloc(msz + 1);
	if (mbox->mod[i]) {
		mbox->modlen[i] = msz;
		memcpy(mbox->mod[i], msg, msz);
		mbox->mod[i][msz] = '\0';
	}
	return 0;
}

int mbox_len(struct mbox *mbox)
{
	return mbox->msgcnt;
}

static void *mextend(void *old, long oldsz, long newsz, long memsz)
{
	void *new = malloc(newsz * memsz);
	memcpy(new, old, oldsz * memsz);
	memset(new + oldsz * memsz, 0, (newsz - oldsz) * memsz);
	free(old);
	return new;
}

static int mbox_extend(struct mbox *mbox, int cnt)
{
	mbox->msgmax = mbox->msgmax + cnt;
	mbox->msg = mextend(mbox->msg, mbox->msgcnt, mbox->msgmax, sizeof(mbox->msg[0]));
	mbox->msglen = mextend(mbox->msglen, mbox->msgcnt, mbox->msgmax, sizeof(mbox->msglen[0]));
	if (!mbox->msg || !mbox->msglen)
		return 1;
	return 0;
}

static int mbox_mails(struct mbox *mbox, char *s, char *e)
{
	s = mbox_from_(s, e);
	while (s && s < e) {
		char *r = s;
		if (mbox->msgcnt == mbox->msgmax)
			if (mbox_extend(mbox, mbox->msgmax ? mbox->msgmax : 256))
				return 1;
		mbox->msg[mbox->msgcnt] = s;
		s = mbox_from_(s + 6, e);
		mbox->msglen[mbox->msgcnt] = s ? s - r : e - r;
		mbox->msgcnt++;
	}
	return 0;
}

static int filesize(int fd)
{
	struct stat stat;
	fstat(fd, &stat);
	return stat.st_size;
}

static char *sdup(char *s)
{
	int n = strlen(s) + 1;
	char *r = malloc(n);
	if (r)
		memcpy(r, s, n);
	return r;
}

static int mbox_read(struct mbox *mbox, char *path)
{
	int tag = mbox->cnt++;
	int fd;
	mbox->boxpath[tag] = sdup(path);
	if (!mbox->boxpath[tag])
		return 1;
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return 1;
	mbox->boxlen[tag] = filesize(fd);
	mbox->boxbuf[tag] = malloc(mbox->boxlen[tag] + 1);
	if (!mbox->boxbuf[tag])
		return 1;
	xread(fd, mbox->boxbuf[tag], mbox->boxlen[tag]);
	mbox->boxbuf[tag][mbox->boxlen[tag]] = '\0';
	close(fd);
	set_atime(mbox->boxpath[tag]);		/* update mbox access time */
	mbox->boxbeg[tag] = mbox->msgcnt;
	if (mbox_mails(mbox, mbox->boxbuf[tag], mbox->boxbuf[tag] + mbox->boxlen[tag]))
		return 1;
	mbox->boxend[tag] = mbox->msgcnt;
	return 0;
}

struct mbox *mbox_open(char **path)
{
	struct mbox *mbox = malloc(sizeof(*mbox));
	if (!mbox)
		return NULL;
	memset(mbox, 0, sizeof(*mbox));
	for (; *path; path++) {
		if (mbox->cnt + 1 < MBOX_N && mbox_read(mbox, *path)) {
			mbox_free(mbox);
			return NULL;
		}
	}
	mbox->mod = calloc(mbox->msgcnt, sizeof(mbox->mod[0]));
	mbox->modlen = calloc(mbox->msgcnt, sizeof(mbox->modlen[0]));
	if (!mbox->mod || !mbox->modlen) {
		mbox_free(mbox);
		return NULL;
	}
	return mbox;
}

void mbox_free(struct mbox *mbox)
{
	int i;
	for (i = 0; i < mbox->cnt; i++) {
		free(mbox->boxpath[i]);
		free(mbox->boxbuf[i]);
	}
	if (mbox->mod) {
		for (i = 0; i < mbox->msgcnt; i++)
			free(mbox->mod[i]);
	}
	free(mbox->msg);
	free(mbox->msglen);
	free(mbox->mod);
	free(mbox->modlen);
	free(mbox);
}

static int mbox_modified(struct mbox *mbox, int tag)
{
	int i;
	for (i = mbox->boxbeg[tag]; i < mbox->boxend[tag]; i++)
		if (mbox->mod[i])
			return 1;
	return 0;
}

int mbox_copy(struct mbox *mbox, char *path)
{
	int fd;
	int i;
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
	if (fd < 0)
		return -1;
	for (i = mbox->boxbeg[0]; i < mbox->boxend[0]; i++) {
		char *msg = mbox->mod[i] ? mbox->mod[i] : mbox->msg[i];
		long len = mbox->mod[i] ? mbox->modlen[i] : mbox->msglen[i];
		xwrite(fd, msg, len);
	}
	close(fd);
	return 0;
}

int mbox_savetag(struct mbox *mbox, int tag)
{
	int fd;
	int i = 0;
	char *newbuf = NULL;
	long off = 0;
	long newlen;
	if (!mbox_modified(mbox, tag))
		return 0;
	for (i = mbox->boxbeg[tag]; i < mbox->boxend[tag] && !mbox->mod[i]; i++)
		off += mbox->msglen[i];
	fd = open(mbox->boxpath[tag], O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	newlen = filesize(fd) - mbox->boxlen[tag];
	if (newlen > 0) {
		newbuf = malloc(newlen);
		lseek(fd, mbox->boxlen[tag], 0);
		xread(fd, newbuf, newlen);
	}
	ftruncate(fd, lseek(fd, off, 0));
	for (; i < mbox->boxend[tag]; i++) {
		char *msg = mbox->mod[i] ? mbox->mod[i] : mbox->msg[i];
		long len = mbox->mod[i] ? mbox->modlen[i] : mbox->msglen[i];
		lseek(fd, 0, 2);
		xwrite(fd, msg, len);
	}
	if (newlen > 0) {
		lseek(fd, 0, 2);
		xwrite(fd, newbuf, newlen);
		free(newbuf);
	}
	close(fd);
	return newlen;
}

int mbox_save(struct mbox *mbox)
{
	return mbox_savetag(mbox, 0);
}

/* return file index of a message and its position within that file */
int mbox_pos(struct mbox *mbox, int n, int *idx)
{
	int i;
	for (i = 1; i < mbox->cnt; i++)
		if (n < mbox->boxbeg[i])
			break;
	*idx = n - mbox->boxbeg[i - 1];
	return i - 1;
}

int mbox_ith(char *path, int n, char **msg, long *msz)
{
	int fd = open(path, O_RDONLY);
	char *s, *e, *r;
	char *buf;
	int len;
	int i;
	if (fd < 0)
		return 1;
	len = filesize(fd);
	buf = malloc(len + 1);
	if (!buf)
		return 1;
	xread(fd, buf, len);
	buf[len] = '\0';
	close(fd);
	e = buf + len;
	s = mbox_from_(buf, e);
	for (i = 0; s && i < n; i++)
		s = mbox_from_(s + 1, e);
	if (!s)
		return 1;
	r = mbox_from_(s + 1, e);
	if (!r)
		r = buf + len;
	*msg = malloc(r - s + 1);
	if (!*msg)
		return 1;
	*msz = r - s;
	memcpy(*msg, s, *msz);
	(*msg)[*msz] = '\0';
	free(buf);
	return 0;
}

int mbox_off(char *path, long beg, long end, char **msg, long *msz)
{
	int fd = open(path, O_RDONLY);
	long nr;
	if (fd < 0)
		return 1;
	if (lseek(fd, beg, 0) < 0) {
		close(fd);
		return 1;
	}
	*msg = malloc(end - beg + 1);
	if (!*msg)
		return 1;
	nr = xread(fd, *msg, end - beg);
	(*msg)[nr] = '\0';
	*msz = nr;
	close(fd);
	return 0;
}
