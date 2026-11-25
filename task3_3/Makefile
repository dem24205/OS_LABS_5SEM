CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread

proxy: proxy.c cache.c cache.h picohttpparser.c picohttpparser.h
	$(CC) $(CFLAGS) -o proxy proxy.c cache.c picohttpparser.c

clean:
	rm -f proxy