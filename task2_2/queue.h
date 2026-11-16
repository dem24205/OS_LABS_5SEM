#ifndef __FITOS_QUEUE_H__
#define __FITOS_QUEUE_H__

#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct {
    atomic_long count;
    sem_t sleepers;
} bigsem_t;

typedef struct _QueueNode {
	int val;
	struct _QueueNode *next;
} qnode_t;

typedef struct _Queue {
	qnode_t *first;
	qnode_t *last;

	pthread_t qmonitor_tid;

	long count;
	long max_count;

	pthread_spinlock_t spin;
	pthread_mutex_t mutex;
	pthread_cond_t not_empty, not_full;
	sem_t lock;               
	bigsem_t slots, items;   

	// queue statistics
	long add_attempts;
	long get_attempts;
	long add_count;
	long get_count;
} queue_t;

queue_t *queue_init(long max_count);
void queue_destroy(queue_t *q);
int queue_add(queue_t *q, int val);
int queue_get(queue_t *q, int *val);
void queue_print_stats(queue_t *q);

void queue_common_init(queue_t *q, long max_count);
void queue_common_destroy(queue_t *q);

qnode_t *qnode_new(int val); 
int qnode_take(qnode_t *n);      

void queue_push(queue_t *q, qnode_t *node);   
qnode_t *queue_pop(queue_t *q);            

#endif		// __FITOS_QUEUE_H__