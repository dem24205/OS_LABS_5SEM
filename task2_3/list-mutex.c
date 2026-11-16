#include "list.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int err, const char *what) {
    if (err) {
        printf("%s failed: %s\n", what, strerror(err));
        abort();
    }
}

void node_lock_init(Node *n) {
	check(pthread_mutex_init(&n->sync.mutex, NULL), "pthread_mutex_init");
}

void node_rdlock(Node *n) {    
	check(pthread_mutex_lock(&n->sync.mutex), "pthread_mutex_lock");
}

void node_wrlock(Node *n) {
	check(pthread_mutex_lock(&n->sync.mutex), "pthread_mutex_lock");
}

void node_unlock(Node *n) {
	check(pthread_mutex_unlock(&n->sync.mutex), "pthread_mutex_unlock");
}