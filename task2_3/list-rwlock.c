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
	check(pthread_rwlock_init(&n->sync.rwlock, NULL), "pthread_rwlock_init");
}
void node_rdlock(Node *n) {
	check(pthread_rwlock_rdlock(&n->sync.rwlock), "pthread_rwlock_rdlock");
}
void node_wrlock(Node *n) {
	check(pthread_rwlock_wrlock(&n->sync.rwlock), "pthread_rwlock_wrlock");
}
void node_unlock(Node *n) {
	check(pthread_rwlock_unlock(&n->sync.rwlock), "pthread_rwlock_unlock");
}