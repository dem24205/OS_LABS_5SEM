#pragma once

typedef struct __uthread *uthread_t;

int uthread_create(uthread_t *t, void *(*start_routine)(void *), void *arg);
void uthread_yield();
void uthread_join_all();
//для учета количества потоков
int uthread_reused_slots();
int uthread_n_threads();
int uthread_used_slots();