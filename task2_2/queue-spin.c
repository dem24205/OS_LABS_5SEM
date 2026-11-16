#define _GNU_SOURCE
#include "queue.h"
#include <pthread.h>

queue_t *queue_init(long max_count) {
	queue_t *q = malloc(sizeof(queue_t));
	if (!q) {
		printf("Cannot allocate memory for a queue\n");
		abort();
	}

	int err = pthread_spin_init(&q->spin, PTHREAD_PROCESS_PRIVATE);
	if (err) {
		printf("queue_init: pthread_spin_init() failed: %s\n", strerror(err));
		abort();
	}
	queue_common_init(q, max_count);
	return q;
}

void queue_destroy(queue_t *q) {
	queue_common_destroy(q);
	pthread_spin_destroy(&q->spin);
	free(q);
}

int queue_add(queue_t *q, int val) {
	q->add_attempts++;                 
	qnode_t *node = qnode_new(val);   

	pthread_spin_lock(&q->spin);
	if (q->count == q->max_count) {
		pthread_spin_unlock(&q->spin);
		free(node);                    
		return 0;
	}
	queue_push(q, node);
	pthread_spin_unlock(&q->spin);

	q->add_count++;
	return 1;
}

int queue_get(queue_t *q, int *val) {
	q->get_attempts++;               

	pthread_spin_lock(&q->spin);
	if (q->count == 0) {
		pthread_spin_unlock(&q->spin);
		return 0;
	}
	qnode_t *node = queue_pop(q);      
	pthread_spin_unlock(&q->spin);

	*val = qnode_take(node);        
	q->get_count++;
	return 1;
}