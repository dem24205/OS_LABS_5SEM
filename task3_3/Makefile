CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread

proxy: proxy.c picohttpparser.c picohttpparser.h
	$(CC) $(CFLAGS) -o proxy proxy.c picohttpparser.c

clean:
	rm -f proxy