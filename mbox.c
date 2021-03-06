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

struct mbox {
	char *path;
	char **msg;	/* messages */
	long *msglen;	/* message lengths */
	char **mod;	/* modified messages */
	long *modlen;	/* modified message lengths */
	int sz;		/* size of arrays */
	char *buf;	/* mbox buffer */
	long len;	/* buf len */
	int n;		/* number of messages */
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
	if (i < 0 || i >= mbox->n)
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
	if (i < 0 || i >= mbox->n)
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
	return mbox->n;
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
	mbox->sz = mbox->sz + cnt;
	mbox->msg = mextend(mbox->msg, mbox->n, mbox->sz, sizeof(mbox->msg[0]));
	mbox->msglen = mextend(mbox->msglen, mbox->n, mbox->sz, sizeof(mbox->msglen[0]));
	mbox->mod = mextend(mbox->mod, mbox->n, mbox->sz, sizeof(mbox->mod[0]));
	mbox->modlen = mextend(mbox->modlen, mbox->n, mbox->sz, sizeof(mbox->modlen[0]));
	if (!mbox->msg || !mbox->msglen || !mbox->mod || !mbox->modlen)
		return 1;
	return 0;
}

static int mbox_mails(struct mbox *mbox, char *s, char *e)
{
	int i;
	s = mbox_from_(s, e);
	for (i = 0; s && s < e; i++) {
		char *r = s;
		if (mbox->n == mbox->sz)
			mbox_extend(mbox, mbox->sz ? mbox->sz : 256);
		mbox->msg[i] = s;
		s = mbox_from_(s + 6, e);
		mbox->msglen[i] = s ? s - r : mbox->buf + mbox->len - r;
		mbox->n++;
	}
	return 0;
}

static int filesize(int fd)
{
	struct stat stat;
	fstat(fd, &stat);
	return stat.st_size;
}

static int mbox_read(struct mbox *mbox)
{
	int fd = open(mbox->path, O_RDONLY);
	if (fd < 0)
		return 1;
	mbox->len = filesize(fd);
	mbox->buf = malloc(mbox->len + 1);
	if (!mbox->buf)
		return 1;
	xread(fd, mbox->buf, mbox->len);
	mbox->buf[mbox->len] = '\0';
	close(fd);
	set_atime(mbox->path);		/* update mbox access time */
	if (mbox_mails(mbox, mbox->buf, mbox->buf + mbox->len))
		return 1;
	return 0;
}

static char *sdup(char *s)
{
	int n = strlen(s) + 1;
	char *r = malloc(n);
	if (r)
		memcpy(r, s, n);
	return r;
}

struct mbox *mbox_open(char *path)
{
	struct mbox *mbox;
	mbox = malloc(sizeof(*mbox));
	if (!mbox)
		return NULL;
	memset(mbox, 0, sizeof(*mbox));
	mbox->path = sdup(path);
	if (!mbox->path || mbox_read(mbox)) {
		mbox_free(mbox);
		return NULL;
	}
	return mbox;
}

void mbox_free(struct mbox *mbox)
{
	int i;
	for (i = 0; i < mbox->n; i++)
		free(mbox->mod[i]);
	free(mbox->msg);
	free(mbox->msglen);
	free(mbox->mod);
	free(mbox->modlen);
	free(mbox->path);
	free(mbox->buf);
	free(mbox);
}

static int mbox_modified(struct mbox *mbox)
{
	int i;
	for (i = 0; i < mbox->n; i++)
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
	for (i = 0; i < mbox->n; i++) {
		char *msg = mbox->mod[i] ? mbox->mod[i] : mbox->msg[i];
		long len = mbox->mod[i] ? mbox->modlen[i] : mbox->msglen[i];
		xwrite(fd, msg, len);
	}
	close(fd);
	return 0;
}

int mbox_save(struct mbox *mbox)
{
	int fd;
	int i = 0;
	char *newbuf;
	long off = 0;
	long newlen;
	if (!mbox_modified(mbox))
		return 0;
	while (i < mbox->n && !mbox->mod[i])
		off += mbox->msglen[i++];
	fd = open(mbox->path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	newlen = filesize(fd) - mbox->len;
	if (newlen > 0) {
		newbuf = malloc(newlen);
		lseek(fd, mbox->len, 0);
		xread(fd, newbuf, newlen);
	}
	ftruncate(fd, lseek(fd, off, 0));
	for (; i < mbox->n; i++) {
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
