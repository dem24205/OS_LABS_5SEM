CC     = gcc
CFLAGS = -Wall -Wextra -g -fPIC

all: libuthread.so test large_test negative_test

libuthread.so: uthread.c uthread.h
	$(CC) $(CFLAGS) -shared -o libuthread.so uthread.c

test: main.c libuthread.so uthread.h
	$(CC) $(CFLAGS) -o test main.c -L. -luthread -Wl,-rpath,.

large_test: large_test.c libuthread.so uthread.h
	$(CC) $(CFLAGS) -o large_test large_test.c -L. -luthread -Wl,-rpath,.

negative_test: negative_test.c libuthread.so uthread.h
	$(CC) $(CFLAGS) -o negative_test negative_test.c -L. -luthread -Wl,-rpath,.

clean:
	rm -f libuthread.so test large_test negative_test