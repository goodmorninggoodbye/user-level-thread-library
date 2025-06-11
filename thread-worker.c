#include "thread-worker.h"
#include "thread_worker_types.h"
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <stdatomic.h>

#define STACK_SIZE 16 * 1024
#define QUANTUM 10 * 1000


LL *ThreadQueue = NULL;
static unsigned int threadID = 0;
static worker_t currentThread;
static tcb *runningBlock;

void exitCleanup(void);

static void schedule();
static void sched_rr();
void mainThreadAdd();

void enqueue(tcb* block){

    LL *temp = malloc(sizeof(LL));
    temp->next = NULL;
    temp->control = block;

    LL *front = ThreadQueue;

    if(front == NULL){
		ThreadQueue = temp;
        return;
    }

    LL *trail = front;
    LL *lead = front->next;
    while(lead != NULL){
        trail = lead;
        lead = lead->next;
    }
    temp->next = lead;
    trail->next = temp;
	ThreadQueue = front;
	return;
}
tcb* dequeue(){
    LL *front = ThreadQueue;
    if(front == NULL)
        return NULL;
    if(front->control->status == READY){
        ThreadQueue = front->next;
        tcb* block = front->control;
        free(front);
        return block;
    }
    LL *trail = front;
    LL *lead = front->next;
    while(lead != NULL && lead->control->status !=READY){
        trail = lead;
        lead = lead->next;
    }
    if(lead == NULL)
        return NULL;
    trail->next = lead->next;
    tcb* block = lead->control;
    free(lead);
    return block;
}
tcb *getBlock(worker_t tid){
    LL *temp = ThreadQueue;
    while (temp != NULL){
        if (temp->control->tid == tid){
            return temp->control;
        }
        temp = temp->next;
    }
    return NULL;
}

int worker_create(worker_t *thread, pthread_attr_t *attr, void *(*function)(void *), void *arg){
    if (!threadID){
        mainThreadAdd();
    }

    currentThread = threadID;
    tcb* threadBlock = malloc(sizeof(tcb));
    threadBlock->tid = threadID;
    threadBlock->waiting = -1;
    threadBlock->value_ptr = NULL;
    threadBlock->retval = NULL;
    *thread = threadID;
    threadID++;
    threadBlock->status = READY;
    enqueue(threadBlock);
    getcontext(&(threadBlock->context));
    threadBlock->context.uc_link = NULL;
    threadBlock->context.uc_stack.ss_sp = malloc(STACK_SIZE);
    threadBlock->context.uc_stack.ss_size = STACK_SIZE;
    makecontext(&(threadBlock->context), (void*) function, 1, arg); 
};

void mainThreadAdd(){
    atexit(exitCleanup);
    currentThread = threadID;
    tcb *mainBlock = malloc(sizeof(tcb));
    mainBlock->tid = currentThread;
    mainBlock->waiting = -1;
    runningBlock = mainBlock;
    threadID++;
    mainBlock->status = READY;
    mainBlock->value_ptr = NULL;
    mainBlock->retval = NULL;

    struct sigaction action;
    struct itimerval timer;

    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = &schedule;
    sigaction(SIGPROF, &action, NULL);

    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = QUANTUM;

    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = QUANTUM;

    setitimer(ITIMER_PROF, &timer, NULL);
    getcontext(&(runningBlock->context));
}

/* give CPU possession to other user-level worker threads voluntarily */
int worker_yield(){
    schedule();
    return 0;
};

/* terminate a thread */
void worker_exit(void *value_ptr){
    runningBlock->status = FINISHED;
    if (runningBlock->value_ptr != NULL){
        *runningBlock->value_ptr = value_ptr;
        runningBlock->status = TERMINATED;
    }
    else {
        runningBlock->retval = value_ptr;
    }
    LL *temp = ThreadQueue;
    while (temp != NULL){
        if (temp->control->waiting == runningBlock->tid && temp->control->status == SCHEDULED){
            temp->control->waiting = -1;
            temp->control->status = READY;
        }
        temp = temp->next;
    }
    schedule();
}

/* Wait for thread termination */
int worker_join(worker_t thread, void **value_ptr){

    int isFinished = 0;
    LL *temp = ThreadQueue;
    while(temp != NULL){
        if(temp->control->tid == thread && temp->control->status == FINISHED)
			isFinished = 1;
        temp = temp->next;
    }
    if (isFinished){
        tcb *block = getBlock(thread);
        if (value_ptr != NULL){
            block->status = TERMINATED;
            *value_ptr = block->retval;
        }
        return 0;
    }
    runningBlock->status = SCHEDULED;
    runningBlock->waiting = thread;
    tcb *block = getBlock(thread);
    block->value_ptr = value_ptr;
    schedule();
    return 0;
};

/* initialize the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t *mutexattr){
    if (!threadID){
        mainThreadAdd();
    }
    mutex->waiting = NULL;
    mutex->lock = 0;

    return 0;
};

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex){

    while(__sync_lock_test_and_set(&(mutex->lock), 1)){
        runningBlock->status = SCHEDULED;
        tcbList* temp = malloc(sizeof(tcbList));
        temp->next = mutex->waiting;
        temp->block = runningBlock;
        mutex->waiting = temp;
        schedule();
    }
    return 0;
};

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex){
    tcbList *temp = mutex->waiting;
    while (temp != NULL)
    {
        temp->block->status = READY;
        tcbList *k = temp;
        temp = temp->next;
        free(k);
    }
    mutex->waiting = NULL;
    mutex->lock = 0;

    return 0;
};

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex){
    // - make sure mutex is not being used
    // - de-allocate dynamic memory created in worker_mutex_init

    return 0;
};


void exitCleanup(void){
    LL *list = ThreadQueue;
    while (list != NULL){
        LL *temp = list;
        list = list->next;
        free(temp->control->context.uc_stack.ss_sp);
        free(temp->control);
        free(temp);
    }
    free(runningBlock);
}

/* scheduler */
static void schedule(){
    sched_rr();
}

static void sched_rr(){
    LL *trail = ThreadQueue;
    LL *lead = trail->next;
    while(lead != NULL){
        if(lead->control->status == TERMINATED){
            trail->next = lead->next;
            free(lead->control->context.uc_stack.ss_sp);
            free(lead->control);
            free(lead);
            lead = trail->next;
            continue;
        }
        trail = lead;
        lead = lead->next;
    }
    trail = ThreadQueue;
    if(trail->control->status == TERMINATED){
        ThreadQueue = trail->next;
        free(trail->control->context.uc_stack.ss_sp);
        free(trail->control);
        free(trail);
    }
    signal(SIGPROF, SIG_IGN);

    tcb *prevThread = runningBlock;
    runningBlock = dequeue(&ThreadQueue);
    if (runningBlock == NULL){
        runningBlock = prevThread;
        return;
    }
    enqueue(prevThread);
    struct sigaction action;
    struct itimerval timer;

    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = &schedule;
    sigaction(SIGPROF, &action, NULL);

    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = QUANTUM;

    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = QUANTUM;
    setitimer(ITIMER_PROF, &timer, NULL);

    swapcontext(&(prevThread->context), &(runningBlock->context));
}
