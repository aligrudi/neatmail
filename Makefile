CC = cc
CFLAGS = -Wall -O2
LDFLAGS =
OBJS = mail.o ex.o mk.o pg.o mbox.o msg.o mime.o util.o sbuf.o regex.o

all: mail
%.o: %.c mail.h
	$(CC) -c $(CFLAGS) $<
mail: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)
clean:
	rm -f *.o mail
