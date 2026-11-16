CC = gcc
RM = rm
CFLAGS = -g -Wall -I.
LIBS = -lpthread

VARIANTS = spin mutex cond sem
BINS = $(foreach v,$(VARIANTS),queue-$(v) queue-$(v)-sleep)

all: $(BINS)

queue-%: queue.h queue-threads.c queue-%.c queue.c
	$(CC) $(CFLAGS) queue-threads.c queue-$*.c queue.c $(LIBS) -o $@
queue-%-sleep: queue.h queue-threads.c queue-%.c queue.c
	$(CC) $(CFLAGS) -DWRITER_USLEEP queue-threads.c queue-$*.c queue.c $(LIBS) -o $@
clean:
	$(RM) -f *.o $(BINS)