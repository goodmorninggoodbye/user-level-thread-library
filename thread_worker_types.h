#ifndef TW_TYPES_H
#define TW_TYPES_H

#include <ucontext.h>

typedef unsigned int worker_t;

#define READY 0
#define SCHEDULED 1
#define FINISHED 2
#define TERMINATED 3


typedef struct TCB{
    worker_t tid;
    int status;
    ucontext_t context;
    worker_t waiting;
    void **value_ptr;
    void *retval;

} tcb;

typedef struct tcbList{
    struct tcbList *next;
    tcb *block;
} tcbList;

typedef struct LL{
    struct LL *next;
    tcb *control;
} LL;

#endif