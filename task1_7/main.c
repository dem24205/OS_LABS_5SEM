#include "uthread.h"
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#define N_VICTIMS 3
#define N_PARENTS 2
#define N 3

void *child_func(void *arg) {
    int num = (int)(uintptr_t)arg;
    for (int i = 0; i < N; ++i) {
        printf("child thread %d: iteration %d\n", num, i);
        uthread_yield();
    }
    printf("child thread %d: done\n", num);
    return NULL;
}

void *parent_func(void *arg) {
    int num = (int)(uintptr_t)arg;
    uthread_t child;
    if (uthread_create(&child, child_func, (void *)(uintptr_t)(100 + num)) == -1) {
        printf("parent thread %d: uthread_create() failed\n", num);
        return NULL;
    }
    for (int i = 0; i < N; ++i) {
        printf("parent thread %d: iteration %d\n", num, i);
        uthread_yield();
    }
    printf("parent thread %d: done\n", num);
    return NULL;
}

void *victim_func(void *arg) {
    int num = (int)(uintptr_t)arg;
    printf("victim thread %d: alive\n", num);
    uthread_yield();
    printf("victim thread %d: done\n", num);
    return NULL;
}

int main(void) {
    printf("main: pid=%d\n", getpid());
    uthread_t victims[N_VICTIMS];
    for (int i = 0; i < N_VICTIMS; ++i) {
        if (uthread_create(&victims[i], victim_func, (void *)(uintptr_t)i) == -1) {
            perror("main: uthread_create() failed");
            return -1;
        }
    }
    uthread_join_all();
    printf("stats: reused=%d n_threads=%d n_used_slots=%d\n", uthread_reused_slots(), uthread_n_threads(),  uthread_used_slots());
    uthread_t parents[N_PARENTS];
    for (int i = 0; i < N_PARENTS; ++i) {
        if (uthread_create(&parents[i], parent_func, (void *)(uintptr_t)i) == -1) {
            perror("main: uthread_create() failed");
            return -1;
        }
    }
    uthread_join_all();
    printf("stats: reused=%d n_threads=%d n_used_slots=%d\n", uthread_reused_slots(), uthread_n_threads(),  uthread_used_slots());
    return 0;
}