#include "uthread.h"
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#define N_ITER    5
#define N_THREADS 1000
#define BATCH     10

void *short_func(void *arg) {
    (void)arg;
    for (int i = 0; i < N_ITER; ++i) {
        uthread_yield();
    }
    return NULL;
}

int main(void) {
    printf("main: pid=%d\n", getpid());
    for (int i = 0; i < N_THREADS; i += BATCH) {
        uthread_t ts[2 * BATCH];
        for (int j = 0; j < BATCH; ++j) {
            if (uthread_create(&ts[j], short_func, (void *)(uintptr_t)(i + j)) == -1) {
                perror("main: uthread_create() failed");
                return -1;
            }
        }
        uthread_join_all();
        printf("batch %d: reused=%d n_used_slots=%d n_threads=%d\n", i / BATCH, uthread_reused_slots(), uthread_used_slots(), uthread_n_threads());
    }
    return 0;
}