#include "cache.h"
#include <stdlib.h>
#include <string.h>

#define ENTRY_INIT_CAP 65536
#define REFS_NEW_ENTRY 1
#define REFS_HOLDER_TABLE 1
#define REFS_HOLDER_CLIENT 1
#define REFS_HOLDER_LOADER 1
#define REFS_ON_CREATE (REFS_HOLDER_TABLE + REFS_HOLDER_CLIENT + REFS_HOLDER_LOADER)
#define BUF_GROWTH_FACTOR 2

static struct entry *table_head = NULL;
static pthread_mutex_t table_mutex = PTHREAD_MUTEX_INITIALIZER;

struct entry *entry_create(void) {
    struct entry *e = calloc(1, sizeof *e);
    if (!e) {
        return NULL;
    }
    e->buf = malloc(ENTRY_INIT_CAP);
    if (!e->buf) {
        free(e);
        return NULL;
    }
    e->cap = ENTRY_INIT_CAP;
    e->state = ENTRY_DOWNLOADING;
    e->refs = REFS_NEW_ENTRY;
    if (pthread_mutex_init(&e->mutex, NULL) != 0) {
        free(e->buf);
        free(e);
        return NULL;
    }
    if (pthread_cond_init(&e->cond, NULL) != 0) {
        pthread_mutex_destroy(&e->mutex);
        free(e->buf);
        free(e);
        return NULL;
    }
    return e;
}

static void entry_destroy(struct entry *e) {
    pthread_mutex_destroy(&e->mutex);
    pthread_cond_destroy(&e->cond);
    free(e->key);
    free(e->buf);
    free(e);
}

void entry_retain(struct entry *e) {
    pthread_mutex_lock(&e->mutex);
    e->refs++;
    pthread_mutex_unlock(&e->mutex);
}

void entry_release(struct entry *e) {
    pthread_mutex_lock(&e->mutex);
    int left = --e->refs;
    pthread_mutex_unlock(&e->mutex);
    if (left == 0) {
        entry_destroy(e);
    }
}

int entry_append(struct entry *e, const char *data, size_t n) {
    pthread_mutex_lock(&e->mutex);
    if (e->len + n > e->cap) {
        size_t ncap = e->cap;
        while (e->len + n > ncap) {
            ncap *= BUF_GROWTH_FACTOR;
        }
        char *nb = realloc(e->buf, ncap);
        if (!nb) {
            pthread_mutex_unlock(&e->mutex);
            return -1;
        }
        e->buf = nb;
        e->cap = ncap;
    }
    memcpy(e->buf + e->len, data, n);
    e->len += n;
    pthread_cond_broadcast(&e->cond);
    pthread_mutex_unlock(&e->mutex);
    return 0;
}

void entry_finish(struct entry *e, enum entry_state st) {
    pthread_mutex_lock(&e->mutex);
    e->state = st;
    pthread_cond_broadcast(&e->cond);
    pthread_mutex_unlock(&e->mutex);
}

ssize_t entry_read(struct entry *e, size_t offset, char *out, size_t max) {
    pthread_mutex_lock(&e->mutex);
    while (e->len == offset && e->state == ENTRY_DOWNLOADING) {
        pthread_cond_wait(&e->cond, &e->mutex);
    }

    ssize_t r;
    if (e->len > offset) {
        size_t n = e->len - offset;
        if (n > max) {
            n = max;
        }
        memcpy(out, e->buf + offset, n);
        r = (ssize_t)n;
    } else if (e->state == ENTRY_DONE) {
        r = 0;
    } else {
        r = -1;
    }
    pthread_mutex_unlock(&e->mutex);
    return r;
}

struct entry *cache_acquire(const char *key, int *created) {
    pthread_mutex_lock(&table_mutex);

    for (struct entry *e = table_head; e != NULL; e = e->next) {
        if (strcmp(e->key, key) == 0) {
            entry_retain(e);
            pthread_mutex_unlock(&table_mutex);
            *created = 0;
            return e;
        }
    }

    struct entry *e = entry_create();
    if (!e) {
        pthread_mutex_unlock(&table_mutex);
        return NULL;
    }
    e->key = strdup(key);
    if (!e->key) {
        pthread_mutex_unlock(&table_mutex);
        entry_release(e);                
        return NULL;
    }
    e->refs = REFS_ON_CREATE;            
    e->in_table = 1;
    e->next = table_head;
    table_head = e;

    pthread_mutex_unlock(&table_mutex);
    *created = 1;
    return e;
}

void cache_remove(struct entry *e) {
    int removed = 0;
    pthread_mutex_lock(&table_mutex);
    if (e->in_table) {
        struct entry **pp = &table_head;
        while (*pp != e) {
            pp = &(*pp)->next;
        }
        *pp = e->next;
        e->in_table = 0;
        removed = 1;
    }
    pthread_mutex_unlock(&table_mutex);
    if (removed) {
        entry_release(e);                
    }
}