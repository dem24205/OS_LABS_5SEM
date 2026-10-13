#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdio.h>

#include "uthread.h"

static void *test_func(void *arg) { 
    (void)arg; 
    return NULL; 
}

int main(void) {
    uthread_t t;
    errno = 0;
    assert(uthread_create(&t, NULL, NULL) == -1);
    assert(errno == EINVAL);
    printf("null start_routine: OK\n");
    errno = 0;
    assert(uthread_create(NULL, test_func, NULL) == -1);
    assert(errno == EINVAL);
    printf("null t: OK\n");
    for (int i = 0; i < 25; ++i) {
        errno = 0;
        int rc = uthread_create(&t, test_func, NULL);
        if (i < 20) {
            assert(rc == 0);
        } else {
            assert(rc == -1);
            assert(errno == EAGAIN);
        }
    }
    printf("exhaustion: OK\n");
    return 0;
}