#include "list.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SWAPPERS 3

enum {
    PAIR_ASC,
    PAIR_DESC,
    PAIR_EQ,
    READERS
};

typedef struct {
    Storage *storage;
    int mode;
} reader_args_t;

typedef struct {
	Storage *storage;
	long len;
	int id;
} swapper_args_t;

static long iterations[READERS];
static long pairs[READERS];
static long swaps[SWAPPERS];

static void *monitor(void *arg) {
	(void)arg;

	while (1) {
		printf("readers (iterations / pairs): asc %ld / %ld | desc %ld / %ld | eq %ld / %ld\n",
			iterations[PAIR_ASC], pairs[PAIR_ASC],
			iterations[PAIR_DESC], pairs[PAIR_DESC],
			iterations[PAIR_EQ], pairs[PAIR_EQ]);
        printf("swappers (swaps): %ld | %ld | %ld\n", swaps[0], swaps[1], swaps[2]);
        sleep(1);
	}
	return NULL;
}

static void *reader(void *arg) {
    reader_args_t *args_t = (reader_args_t *)arg;
    Node *head = args_t->storage->first;
    int mode = args_t->mode;
    while (1) {
        long cnt = 0;

        Node *cur = head;
        node_rdlock(cur);
        while (cur->next) {
            Node *next = cur->next;
            node_rdlock(next);

            if (cur != head) {
                size_t la = strlen(cur->value);
                size_t lb = strlen(next->value);
                if ((mode == PAIR_ASC && la < lb) ||
                    (mode == PAIR_DESC && la > lb) ||
                    (mode == PAIR_EQ && la == lb)) {
                        cnt++;
                    }
            }
            node_unlock(cur);
            cur = next;
        }
        node_unlock(cur);
        pairs[mode] = cnt;
        iterations[mode]++;
    }
    return NULL;
}

static void *swapper(void *arg) {
	swapper_args_t *args_t = (swapper_args_t *)arg;
	Node *head = args_t->storage->first;
	unsigned int seed = 1000 + args_t->id;

	while (1) {
		if (rand_r(&seed) % 2 == 0)
			continue;

		long pos = rand_r(&seed) % (args_t->len - 1);

		Node *prev = head;
        if (pos == 0) {
            node_wrlock(prev); 
        } 
        else {
            node_rdlock(prev);
        } 
        for (long i = 0; i < pos; i++) {
	        Node *next = prev->next;
            if (i == pos - 1) {
                node_wrlock(next);
            } 
            else {
                node_rdlock(next);
            }   
            node_unlock(prev);
            prev = next;
        }

		Node *x = prev->next;
		node_wrlock(x);
		Node *y = x->next;
		node_wrlock(y);

		prev->next = y;
		x->next = y->next;
		y->next = x;

		node_unlock(y);
		node_unlock(x);
		node_unlock(prev);

		swaps[args_t->id]++;
	}
	return NULL;
}

static void start(void *(*fn)(void *), void *arg) {
	pthread_t tid;
	int err = pthread_create(&tid, NULL, fn, arg);
	if (err) {
		printf("main: pthread_create() failed: %s\n", strerror(err));
		abort();
	}
}

int main(int argc, char **argv) {
	long len = 1000;
	if (argc > 1)
		len = atol(argv[1]);
	if (len < 2) {
		printf("list length must be at least 2\n");
		return -1;
	}

	Storage *storage = storage_init(len);  
	
	static reader_args_t rargs[READERS];
	static swapper_args_t sargs[SWAPPERS];

	for (int i = 0; i < READERS; i++) {
		rargs[i].storage = storage;
		rargs[i].mode = i;
		start(reader, &rargs[i]);
	}
	for (int i = 0; i < SWAPPERS; i++) {
		sargs[i].storage = storage;
		sargs[i].len = len;
		sargs[i].id = i;
		start(swapper, &sargs[i]);
	}
	start(monitor, NULL);
	pthread_exit(NULL);
	return 0;
}