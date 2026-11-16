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
	check(pthread_spin_init(&n->sync.spin, PTHREAD_PROCESS_PRIVATE), "pthread_spin_init");
}
void node_rdlock(Node *n) {     
	check(pthread_spin_lock(&n->sync.spin), "pthread_spin_lock");
}
void node_wrlock(Node *n) {
	check(pthread_spin_lock(&n->sync.spin), "pthread_spin_lock");
}
void node_unlock(Node *n) {
	check(pthread_spin_unlock(&n->sync.spin), "pthread_spin_unlock");
}