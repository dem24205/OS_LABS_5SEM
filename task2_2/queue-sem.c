#define _GNU_SOURCE
#include "queue.h"
#include <pthread.h>

static int bigsem_init(bigsem_t *s, long value) {
	if (value < 0) {
		errno = EINVAL;
		return -1;
	}
	atomic_init(&s->count, value);    
	return sem_init(&s->sleepers, 0, 0); 
}

static void bigsem_destroy(bigsem_t *s) {
	sem_destroy(&s->sleepers);
}

static void bigsem_wait(bigsem_t *s) {
	long old = atomic_fetch_sub(&s->count, 1);  
	if (old <= 0)                                
		sem_wait(&s->sleepers);                
}

static void bigsem_post(bigsem_t *s) {
	long old = atomic_fetch_add(&s->count, 1);   
	if (old < 0)                              
		sem_post(&s->sleepers);            
}


static void die(const char *what) {
	printf("queue_init: %s failed: %s\n", what, strerror(errno));
	abort();
}

queue_t* queue_init(long max_count) {
	queue_t *q = malloc(sizeof(queue_t));
	if (!q) {
		printf("Cannot allocate memory for a queue\n");
		abort();
	}
	if (sem_init(&q->lock, 0, 1) == -1)          
		die("sem_init(lock)");
	if (bigsem_init(&q->slots, max_count) == -1)
		die("bigsem_init(slots)");
	if (bigsem_init(&q->items, 0) == -1)       
		die("bigsem_init(items)");
	queue_common_init(q, max_count);
	return q;
}

void queue_destroy(queue_t *q) {
	queue_common_destroy(q);
	sem_destroy(&q->lock);
	bigsem_destroy(&q->slots);
	bigsem_destroy(&q->items);
	free(q);
}

int queue_add(queue_t *q, int val) {
	q->add_attempts++;
	qnode_t *node = qnode_new(val);   

	bigsem_wait(&q->slots);          
	sem_wait(&q->lock);
	queue_push(q, node);
	sem_post(&q->lock);
	bigsem_post(&q->items);   

	q->add_count++;
	return 1;
}

int queue_get(queue_t *q, int *val) {
	q->get_attempts++;

	bigsem_wait(&q->items);           
	sem_wait(&q->lock);
	qnode_t *node = queue_pop(q);
	sem_post(&q->lock);
	bigsem_post(&q->slots);           

	*val = qnode_take(node); 
	q->get_count++;
	return 1;
}