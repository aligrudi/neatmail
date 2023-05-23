#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "mail.h"

long xread(int fd, void *buf, long len)
{
	int nr = 0;
	while (nr < len) {
		int ret = read(fd, buf + nr, len - nr);
		if (ret == -1 && (errno == EAGAIN || errno == EINTR))
			continue;
		if (ret <= 0)
			break;
		nr += ret;
	}
	return nr;
}

long xwrite(int fd, void *buf, long len)
{
	int nw = 0;
	while (nw < len) {
		int ret = write(fd, buf + nw, len - nw);
		if (ret == -1 && (errno == EAGAIN || errno == EINTR))
			continue;
		if (ret < 0)
			break;
		nw += ret;
	}
	return nw;
}

static int xpipe_make(char **argv, int *ifd, int *ofd)
{
	int pid;
	int pipefds0[2];
	int pipefds1[2];
	if (ifd)
		pipe(pipefds0);
	if (ofd)
		pipe(pipefds1);
	if (!(pid = fork())) {
		if (ifd) {		/* setting up stdin */
			close(0);
			dup(pipefds0[0]);
			close(pipefds0[1]);
			close(pipefds0[0]);
		}
		if (ofd) {		/* setting up stdout */
			close(1);
			dup(pipefds1[1]);
			close(pipefds1[0]);
			close(pipefds1[1]);
		}
		execvp(argv[0], argv);
		exit(1);
	}
	if (ifd)
		close(pipefds0[0]);
	if (ofd)
		close(pipefds1[1]);
	if (pid < 0) {
		if (ifd)
			close(pipefds0[1]);
		if (ofd)
			close(pipefds1[0]);
		return -1;
	}
	if (ifd)
		*ifd = pipefds0[1];
	if (ofd)
		*ofd = pipefds1[0];
	return pid;
}

int xpipe(char *cmd, char *ibuf, long ilen, char **obuf, long *olen)
{
	char *argv[] = {"/bin/sh", "-c", cmd, NULL};
	struct pollfd fds[2];
	struct sbuf *sb = NULL;
	char buf[512];
	int ifd = -1, ofd = -1;
	int nw = 0;
	int pid = xpipe_make(argv, ibuf ? &ifd : NULL, obuf ? &ofd : NULL);
	if (pid <= 0)
		return 1;
	if (obuf)
		sb = sbuf_make();
	fcntl(ifd, F_SETFL, fcntl(ifd, F_GETFL, 0) | O_NONBLOCK);
	fds[0].fd = ofd;
	fds[0].events = POLLIN;
	fds[1].fd = ifd;
	fds[1].events = POLLOUT;
	while ((fds[0].fd >= 0 || fds[1].fd >= 0) && poll(fds, 2, 200) >= 0) {
		if (fds[0].revents & POLLIN) {
			int ret = read(fds[0].fd, buf, sizeof(buf));
			if (ret > 0)
				sbuf_mem(sb, buf, ret);
			if (ret <= 0) {
				close(fds[0].fd);
				fds[0].fd = -1;
			}
		} else if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
			fds[0].fd = -1;
		}
		if (fds[1].revents & POLLOUT) {
			int ret = write(fds[1].fd, ibuf + nw, ilen - nw);
			if (ret > 0)
				nw += ret;
			if (ret <= 0 || nw == ilen) {
				close(fds[1].fd);
				fds[1].fd = -1;
			}
		} else if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
			fds[1].fd = -1;
		}
	}
	close(ifd);
	close(ofd);
	waitpid(pid, NULL, 0);
	if (obuf) {
		*olen = sbuf_len(sb);
		*obuf = sbuf_done(sb);
	}
	return 0;
}
