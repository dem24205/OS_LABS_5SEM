#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <ucontext.h>
#include <stdint.h>

#include "uthread.h"

#define THREAD_STACK_SIZE (64 * 1024)
#define MAX_THREADS 20

struct __uthread {
    char stack[THREAD_STACK_SIZE];
    ucontext_t ctx;
    void *(*start_routine)(void *);
    void *arg;
    bool is_finished;
};

static struct __uthread slots[MAX_THREADS];
static struct __uthread main_t;
static uthread_t threads[MAX_THREADS + 1] = { &main_t };
static int n_threads = 1;
static int cur_t = 0;
static int n_used_slots = 0;
static int reused_slots = 0;

void uthread_yield() {
    int prev = cur_t;
    for (int i = 1; i < n_threads; ++i) {
        int next = (prev + i) % n_threads;
        if (!threads[next]->is_finished) {
            cur_t = next;
            if (swapcontext(&threads[prev]->ctx, &threads[next]->ctx) == -1) {
                return;
            }
        }
    }
}

static void startup(void) {
    uthread_t self = threads[cur_t];
    self->start_routine(self->arg);
    self->is_finished = true;
    uthread_yield();
}

int uthread_create(uthread_t *t, void*(*start_routine)(void *), void *arg) {
    if (!t) {
        errno = EINVAL;
        return -1;
    }
    if (!start_routine) {
        errno = EINVAL;
        return -1;
    }
    int slot = -1;
    bool is_new_slot = false;
    for (int i = 0; i < n_used_slots; ++i) {
        if (slots[i].is_finished) {
            slot = i;
            reused_slots++;
            break;
        }
    }
    if (slot == -1) {
        if (n_used_slots >= MAX_THREADS) {
            errno = EAGAIN;
            return -1;
        }
        slot = n_used_slots++;
        assert(n_used_slots <= MAX_THREADS);
        is_new_slot = true;
    }
    uthread_t thread = &slots[slot];
    if (getcontext(&thread->ctx) == -1) return -1;
    thread->ctx.uc_stack.ss_sp = thread->stack;
    thread->ctx.uc_stack.ss_size = THREAD_STACK_SIZE;
    thread->ctx.uc_link = NULL;
    makecontext(&thread->ctx, startup, 0);
    thread->start_routine = start_routine;
    thread->arg = arg;
    thread->is_finished = false;
    if (is_new_slot) threads[n_threads++] = thread;
    *t = thread;
    return 0;
}

void uthread_join_all() {
    bool done;
    int i;
    do {
        uthread_yield();
        for (done = 1, i = 1; i < n_threads; ++i) {
            done &= threads[i]->is_finished;
        }
    }
    while (!done);
}

int uthread_reused_slots() {
    return reused_slots;
}

int uthread_n_threads() {
    return n_threads;
}

int uthread_used_slots() {
    return n_used_slots;
}