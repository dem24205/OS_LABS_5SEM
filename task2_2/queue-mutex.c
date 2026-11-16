#define _GNU_SOURCE
#include "queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

static void q_lock(queue_t *q, const char *who) {
	int err = pthread_mutex_lock(&q->mutex);
	if (err) {
		fprintf(stderr, "%s: pthread_mutex_lock failed: %s\n", who, strerror(err));
		abort();
	}
}

static void q_unlock(queue_t *q, const char *who) {
	int err = pthread_mutex_unlock(&q->mutex);
	if (err) {
		fprintf(stderr, "%s: pthread_mutex_unlock failed: %s\n", who, strerror(err));
		abort();
	}
}

queue_t *queue_init(long max_count) {
	queue_t *q = malloc(sizeof(queue_t));
	if (!q) {
		fprintf(stderr, "queue_init: cannot allocate memory: %s\n", strerror(errno));
		abort();
	}

	int err = pthread_mutex_init(&q->mutex, NULL);
	if (err) {
		fprintf(stderr, "queue_init: pthread_mutex_init failed: %s\n", strerror(err));
		free(q);
		abort();
	}

	queue_common_init(q, max_count);
	return q;
}

void queue_destroy(queue_t *q) {
	queue_common_destroy(q);
	int err = pthread_mutex_destroy(&q->mutex);
	if (err) {
		fprintf(stderr, "queue_destroy: pthread_mutex_destroy failed: %s\n", strerror(err));
		abort();
	}
	free(q);
}

int queue_add(queue_t *q, int val) {
	q->add_attempts++;
	qnode_t *node = qnode_new(val);    

	q_lock(q, "queue_add");
	if (q->count == q->max_count) {
		q_unlock(q, "queue_add");
		free(node);
		return 0;
	}
	queue_push(q, node);
	q_unlock(q, "queue_add");

	q->add_count++;
	return 1;
}

int queue_get(queue_t *q, int *val) {
	q->get_attempts++;

	q_lock(q, "queue_get");
	if (q->count == 0) {
		q_unlock(q, "queue_get");
		return 0;
	}
	qnode_t *node = queue_pop(q);
	q_unlock(q, "queue_get");

	*val = qnode_take(node);         
	q->get_count++;
	return 1;
}