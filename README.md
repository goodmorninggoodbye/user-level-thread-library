Ewan Jee         ej307
Paul Samoylov    ps1172

1.
TCB: The tcb struct, which has tid (threadId), status (representing the status of this tcb), context, waiting (this is for worker_join), value_ptr and retval.
Mutex: The worker_mutex_t struct, which has a waiting list of threads that are awaiting the mutex and a flag (lock) indicating whether the mutex is locked, serves as a representation of the mutex.
Runqueues: The run queue is implemented as a linked list (LL), where each node holds a pointer to the next node in the queue (next) and a pointer to a TCB (control). 
Acts like a queue for enqueue and dequeue functions.

2.
worker_create: Inserts a newly allocated TCB into the running queue, sets its stack and context, and enqueues it.
worker_yield: Requests a voluntary release of CPU possession from the scheduler by calling schedule function.
worker_exit: Calls the scheduler, adjusts the waiting state of other threads as needed, and marks the current thread as completed or terminated.
worker_join: Uses the scheduler to wait if needed, and checks the status of the provided thread in the running queue to wait for it to end.
worker_mutex_init: Sets a mutex's waiting list to NULL and its lock flag to 0 (unlocked) to initialize it.
worker_mutex_lock: Uses atomic operation __sync_lock_test_and_set to check and modify the lock. The thread is added to the mutex's waiting list and the scheduler is notified to relinquish the CPU if the mutex is already locked.
worker_mutex_unlock: Clears the waiting list, releases the mutex lock, and sets the status of every thread that is waiting to READY.

3.
Choosing which thread to execute next and maintaining thread states are the responsibilities of the scheduler (schedule function). 
It carries out the subsequent tasks: removes any terminated threads from the queue of active threads.
Instructs the timer to disregard interruptions while scheduling in order to avoid reentry.
The current thread (runningThread) is dequeued to become the next READY thread from the running queue.
The preceding thread keeps running if there isn't a ready thread discovered.
Returns the previously executed thread to the ongoing queue.
Configures the signal handler and timer for the subsequent quantum.
Changes the running thread's context to match that of the preceding thread.
With this implementation, the round-robin scheduling technique is used, giving each thread a fixed amount of time to run before scheduling the next thread. 
The next READY thread is chosen for execution by the scheduler, which also makes sure that threads in the TERMINATED state are eliminated from the queue.

4. Collaboration and References:
• Recitation 5 Slides
    o Thread APIs, mutex implementation, etc.
• https://man7.org/linux/man-pages/man3/makecontext.3.html
    o Make and swap context
• https://www.informit.com/articles/article.aspx?p=23618&seqNum=14
    o Setting interval timers
• Linux.die.net/man/
    o General use; mentioned in pdf
• https://stackoverflow.com/
    o Bug fixing, clarification on errors, etc.