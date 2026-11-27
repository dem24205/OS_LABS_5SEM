#ifndef CACHE_H
#define CACHE_H

#include <pthread.h>
#include <stddef.h>
#include <sys/types.h>

enum entry_state {
    ENTRY_DOWNLOADING,   
    ENTRY_DONE,          
    ENTRY_FAILED
};

struct entry {
    char *buf;            
    size_t len;              
    size_t cap;             
    enum entry_state state;
    int refs; //сколько потоков держат запись              
    pthread_mutex_t mutex;
    pthread_cond_t cond;     

    char *key;           
    struct entry *next;     
    int in_table; //флаг нахождения в таблице
};

struct entry *entry_create(void);             
void entry_retain(struct entry *e);            
void entry_release(struct entry *e);       
int entry_append(struct entry *e, const char *data, size_t n);
void entry_finish(struct entry *e, enum entry_state st);
ssize_t entry_read(struct entry *e, size_t offset, char *out, size_t max);

struct entry *cache_acquire(const char *key, int *created);
void cache_remove(struct entry *e);

#endif