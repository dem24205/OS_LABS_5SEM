#ifndef __FITOS_LIST_H__
#define __FITOS_LIST_H__

#define _GNU_SOURCE
#include <pthread.h>

#define MAX_LEN 100

typedef union {
    pthread_mutex_t mutex;
    pthread_spinlock_t spin;
    pthread_rwlock_t rwlock;
} sync_t;

typedef struct _Node {
    char value[MAX_LEN];
    struct _Node *next;
    sync_t sync;
} Node;

typedef struct _Storage {
    Node *first;
} Storage;

Node *node_create(const char *str);

Storage *storage_init(long n);

void node_lock_init(Node *n);
void node_rdlock(Node *n);
void node_wrlock(Node *n);
void node_unlock(Node *n);

#endif