CC = gcc
CFLAGS = -Wall -Wextra -g -fPIC

all: libuthread.so test

libuthread.so: uthread.c uthread.h
	$(CC) $(CFLAGS) -shared -o libuthread.so uthread.c

test: main.c libuthread.so uthread.h
	$(CC) $(CFLAGS) -o test main.c -L. -luthread -Wl,-rpath,.

clean:
	rm -f libuthread.so test