#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <bits/pthreadtypes.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <stdlib.h>

void store_id(pthread_t  * id) {
    fprintf(stderr, "new thread created with id %lu\n", (*id));
}

#undef pthread_create

typedef struct {
    void* arg;
    void * (*start)(void *);
} Pair;

void* callbackToStart(void* arg) {
    Pair* pair = (Pair*) arg;
    fprintf(stderr, "new thread started with tid %ld\n", syscall(SYS_gettid));

    void* result = pair->start(pair->arg);

    free(pair);

    return result;
}

int pthread_create(pthread_t * thread, pthread_attr_t * attr, void * (*start)(void *), void * arg)
{
    int rc;
    static int (*real_create)(pthread_t * , pthread_attr_t *, void * (*start)(void *), void *) = NULL;
    if (!real_create)
        real_create = dlsym(RTLD_NEXT, "pthread_create");

    Pair* pair = malloc(sizeof(Pair));
    pair->start = start;
    pair->arg = arg;

    rc = real_create(thread, attr, &callbackToStart, pair);
    if(!rc) {
        store_id(thread);
    }

    return rc;
}

