CC = cc
CFLAGS = -Wall -O2
LDFLAGS =
OBJS = mail.o ex.o mk.o pg.o me.o pn.o mbox.o msg.o mime.o util.o sbuf.o regex.o

all: mail
%.o: %.c mail.h
	$(CC) -c $(CFLAGS) $<
mail: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)
check: mail
	$(SHELL) test.sh
clean:
	rm -f *.o mail
