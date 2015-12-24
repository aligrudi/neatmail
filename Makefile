CC = cc
CFLAGS = -Wall -O2
LDFLAGS =
OBJS = mail.o ex.o mk.o mbox.o msg.o util.o sbuf.o

all: mail
%.o: %.c mail.h
	$(CC) -c $(CFLAGS) $<
mail: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)
clean:
	rm -f *.o mail
