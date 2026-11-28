#include "cache.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define ENTRY_INIT_CAP 65536
#define REFS_NEW_ENTRY 1
#define REFS_HOLDER_TABLE 1
#define REFS_HOLDER_CLIENT 1
#define REFS_HOLDER_LOADER 1
#define REFS_ON_CREATE (REFS_HOLDER_TABLE + REFS_HOLDER_CLIENT + REFS_HOLDER_LOADER)
#define BUF_GROWTH_FACTOR 2
#define GC_WATERMARK_PERCENT 85
#define PERCENT_TOTAL 100

static struct entry *table_head = NULL;
static pthread_mutex_t table_mutex = PTHREAD_MUTEX_INITIALIZER;

static uint64_t lru_clock = 0;
static size_t total_cache_size = 0;    
static size_t max_cache_size = 0; 
static size_t gc_watermark = 0;
static pthread_cond_t gc_need_cond = PTHREAD_COND_INITIALIZER; 

static pthread_cond_t gc_done_cond = PTHREAD_COND_INITIALIZER; 
static int gc_requested = 0;         
static unsigned long gc_epoch = 0;
static size_t gc_needed = 0;

static void wake_monitor(void) {
    if (total_cache_size >= gc_watermark) {
        gc_requested = 1;
        pthread_cond_signal(&gc_need_cond);
    }
}

static void wait_for_monitor(size_t need) {
    if (gc_needed < need) {
        gc_needed = need;
    }
    unsigned long my_epoch = gc_epoch;
    gc_requested = 1;
    pthread_cond_signal(&gc_need_cond);
    while (gc_epoch == my_epoch) {
        pthread_cond_wait(&gc_done_cond, &table_mutex);
    }
}

static int cache_reserve(size_t delta, size_t size_after) {
    pthread_mutex_lock(&table_mutex);

    if (size_after > max_cache_size) { 
        fprintf(stderr, "[cache] reject: entry would be %zu KB, cache limit is %zu KB\n",
                size_after / 1024, max_cache_size / 1024);
        pthread_mutex_unlock(&table_mutex);
        return -1;
    }
    if (total_cache_size + delta > max_cache_size) {
        wait_for_monitor(delta);
        if (total_cache_size + delta > max_cache_size) {
            fprintf(stderr, "[cache] reject: need %zu KB, used %zu of %zu KB, nothing to evict\n",
                    delta / 1024, total_cache_size / 1024, max_cache_size / 1024);
            pthread_mutex_unlock(&table_mutex);
            return -1;
        }
    }

    total_cache_size += delta;
    wake_monitor();
    fprintf(stderr, "[cache] used %zu KB\n", total_cache_size / 1024);
    pthread_mutex_unlock(&table_mutex);
    return 0;
}

static void cache_unreserve(size_t bytes) {
    pthread_mutex_lock(&table_mutex);
    total_cache_size -= bytes;
    fprintf(stderr, "[cache] used %zu KB\n", total_cache_size / 1024);
    pthread_mutex_unlock(&table_mutex);
}

void cache_set_limit(size_t bytes) {
    pthread_mutex_lock(&table_mutex);
    max_cache_size = bytes;
    gc_watermark = bytes / PERCENT_TOTAL * GC_WATERMARK_PERCENT;
    fprintf(stderr, "[cache] limit %zu KB, gc from %zu KB\n",
            max_cache_size / 1024, gc_watermark / 1024);
    pthread_mutex_unlock(&table_mutex);
}

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
    cache_unreserve(e->cap); 
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
    size_t need = e->len + n;
    size_t old_cap = e->cap;
    pthread_mutex_unlock(&e->mutex);

    size_t new_cap = old_cap;
    while (need > new_cap) {
        new_cap *= 2;
    }
    size_t delta = new_cap - old_cap;
    if (delta > 0 && cache_reserve(delta, new_cap) != 0) {
        return -1;
    }
    pthread_mutex_lock(&e->mutex);
    if (delta > 0) {
        char *nb = realloc(e->buf, new_cap);
        if (!nb) {
            pthread_mutex_unlock(&e->mutex);
            cache_unreserve(delta);  
            return -1;
        }
        e->buf = nb;
        e->cap = new_cap;
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

    int waited = 0;
    for (;;) {
        for (struct entry *e = table_head; e != NULL; e = e->next) {
            if (strcmp(e->key, key) == 0) {
                e->last_used = ++lru_clock;
                entry_retain(e);
                pthread_mutex_unlock(&table_mutex);
                *created = 0;
                return e;
            }
        }
        if (total_cache_size + ENTRY_INIT_CAP <= max_cache_size) {
            break;                          
        }
        if (waited) {
            fprintf(stderr, "[cache] reject %s: used %zu of %zu KB, nothing to evict\n",
                    key, total_cache_size / 1024, max_cache_size / 1024);
            pthread_mutex_unlock(&table_mutex);
            return NULL;
        }
        wait_for_monitor(ENTRY_INIT_CAP);
        waited = 1;
    }

    char *key_copy = strdup(key);
    if (!key_copy) {
        pthread_mutex_unlock(&table_mutex);
        return NULL;
    }
    struct entry *e = entry_create();
    if (!e) {
        free(key_copy);
        pthread_mutex_unlock(&table_mutex);
        return NULL;
    }
    e->key = key_copy;
    e->refs = REFS_ON_CREATE;
    e->in_table = 1;
    e->last_used = ++lru_clock;
    e->next = table_head;
    table_head = e;
    total_cache_size += e->cap;
    wake_monitor();

    pthread_mutex_unlock(&table_mutex);
    *created = 1;
    return e;
}

static void unlink_entry(struct entry *e) {
    struct entry **pp = &table_head;
    while (*pp != e) {
        pp = &(*pp)->next;
    }
    *pp = e->next;
    e->in_table = 0;
}

void cache_remove(struct entry *e) {
    int removed = 0;
    pthread_mutex_lock(&table_mutex);
    if (e->in_table) {
        unlink_entry(e);
        removed = 1;
    }
    pthread_mutex_unlock(&table_mutex);
    if (removed) {
        entry_release(e);                
    }
}

static struct entry *pick_victim(void) {
    struct entry *victim = NULL;
    for (struct entry *e = table_head; e != NULL; e = e->next) {
        pthread_mutex_lock(&e->mutex);
        int evictable = (e->state == ENTRY_DONE && e->refs == REFS_HOLDER_TABLE);
        pthread_mutex_unlock(&e->mutex);
        if (evictable && (victim == NULL || e->last_used < victim->last_used)) {
            victim = e;
        }
    }
    return victim;
}

static void *monitor_thread(void *arg) {
    (void)arg;
    pthread_mutex_lock(&table_mutex);
    for (;;) {
        while (!gc_requested) {
            pthread_cond_wait(&gc_need_cond, &table_mutex);
        }
        gc_requested = 0;

        while (total_cache_size >= gc_watermark ||
               total_cache_size + gc_needed > max_cache_size) {
            struct entry *victim = pick_victim();
            if (!victim) {
                break;                      
            }
            unlink_entry(victim);
            fprintf(stderr, "[gc] evict %s\n", victim->key);

            pthread_mutex_unlock(&table_mutex);
            entry_release(victim);         
            pthread_mutex_lock(&table_mutex);
        }

        gc_needed = 0;
        gc_epoch++;
        pthread_cond_broadcast(&gc_done_cond);
    }
    return NULL;
}

int cache_start_monitor(void) {
    pthread_t tid;
    if (pthread_create(&tid, NULL, monitor_thread, NULL) != 0) {
        perror("pthread_create monitor");
        return -1;
    }
    pthread_detach(tid);
    return 0;
}