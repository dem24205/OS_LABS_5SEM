#define _GNU_SOURCE

#include "queue.h"

#include <pthread.h>

void *qmonitor(void *arg) {
	queue_t *q = (queue_t *)arg;

	printf("qmonitor: [%d %d %d]\n", getpid(), getppid(), gettid());

	while (1) {
		queue_print_stats(q);
		sleep(1);
	}

	return NULL;
}

void queue_common_init(queue_t *q, long max_count) {
	int err;
	q->first = NULL;
	q->last = NULL;
	q->max_count = max_count;
	q->count = 0;

	q->add_attempts = q->get_attempts = 0;
	q->add_count = q->get_count = 0;

	err = pthread_create(&q->qmonitor_tid, NULL, qmonitor, q);
	if (err) {
		printf("queue_init: pthread_create() failed: %s\n", strerror(err));
		abort();
	}
}


void queue_common_destroy(queue_t *q) {
	int err;

	err = pthread_cancel(q->qmonitor_tid);
	if (err) {
		printf("queue_destroy: pthread_cancel() failed: %s\n", strerror(err));
		abort();
	}
	err = pthread_join(q->qmonitor_tid, NULL);
	if (err) {
		printf("queue_destroy: pthread_join() failed: %s\n", strerror(err));
		abort();
	}

	qnode_t *cur = q->first;
	while (cur) {
		qnode_t *next = cur->next;
		free(cur);
		cur = next;
	}
}


qnode_t *qnode_new(int val) {
	qnode_t *n = malloc(sizeof(qnode_t));
	if (!n) {
		printf("Cannot allocate memory for new node\n");
		abort();
	}
	n->val = val;
	n->next = NULL;
	return n;
}

int qnode_take(qnode_t *n) {
	int val = n->val;
	free(n);
	return val;
}

void queue_push(queue_t *q, qnode_t *node) {
	if (!q->first)
		q->first = q->last = node;
	else {
		q->last->next = node;
		q->last = node;
	}
	q->count++;
}

qnode_t *queue_pop(queue_t *q) {
	qnode_t *node = q->first;

	q->first = node->next;
	if (!q->first)
		q->last = NULL;   
	q->count--;
	node->next = NULL;
	return node;
}

void queue_print_stats(queue_t *q) {
	printf("queue stats: current size %ld; attempts: (%ld %ld %ld); counts (%ld %ld %ld)\n",
		q->count,
		q->add_attempts, q->get_attempts, q->add_attempts - q->get_attempts,
		q->add_count, q->get_count, q->add_count - q->get_count);
}