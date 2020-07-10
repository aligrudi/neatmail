/* neatmail header */

int ex(char *argv[]);
int mk(char *argv[]);
int pg(char *argv[]);
int me(char *argv[]);

struct mbox;

struct mbox *mbox_open(char *path);
void mbox_free(struct mbox *mbox);
int mbox_save(struct mbox *mbox);
int mbox_copy(struct mbox *mbox, char *path);
int mbox_len(struct mbox *mbox);
int mbox_get(struct mbox *mbox, int i, char **msg, long *msz);
int mbox_set(struct mbox *mbox, int i, char *msg, long msz);
int mbox_ith(char *path, int n, char **msg, long *msz);

char *msg_get(char *msg, long msz, char *hdr);
int msg_set(char *msg, long msglen, char **mod, long *modlen, char *hdr, char *val);
char *msg_hdrdec(char *hdr);
int msg_demime(char *msg, long msglen, char **mod, long *modlen);

int hdrlen(char *hdr, long len);
int startswith(char *r, char *s);
long xread(int fd, void *buf, long len);
long xwrite(int fd, void *buf, long len);
int xpipe(char *cmd, char *ibuf, long ilen, char **obuf, long *olen);

struct sbuf *sbuf_make(void);
char *sbuf_buf(struct sbuf *sb);
char *sbuf_done(struct sbuf *sb);
void sbuf_chr(struct sbuf *sbuf, int c);
void sbuf_mem(struct sbuf *sbuf, char *s, int len);
void sbuf_str(struct sbuf *sbuf, char *s);
int sbuf_len(struct sbuf *sbuf);
void sbuf_printf(struct sbuf *sbuf, char *s, ...);
void sbuf_free(struct sbuf *sb);
