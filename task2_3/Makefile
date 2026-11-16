CC = gcc
RM = rm
CFLAGS = -g -Wall -I.
LIBS = -lpthread

VARIANTS = mutex spin rwlock
BINS = $(foreach v,$(VARIANTS),list-$(v))

all: $(BINS)

list-%: list.h list.c list-threads.c list-%.c
	$(CC) $(CFLAGS) list.c list-threads.c list-$*.c $(LIBS) -o $@

clean:
	$(RM) -f *.o $(BINS)