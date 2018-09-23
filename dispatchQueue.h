/* 
 * File:   dispatchQueue.h
 * Author: robert
 *
 * Modified by: your UPI
 */

#ifndef DISPATCHQUEUE_H
#define	DISPATCHQUEUE_H

#include <pthread.h>
#include <semaphore.h>
#include <sys/sysinfo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define error_exit(MESSAGE)     perror(MESSAGE), exit(EXIT_FAILURE)

    typedef enum { // whether dispatching a task synchronously or asynchronously
        ASYNC, SYNC
    } task_dispatch_type_t;
    
    typedef enum { // The type of dispatch queue.
        CONCURRENT, SERIAL
    } queue_type_t;

    typedef struct task {
        char name[64];              // to identify it when debugging
        void (* work)(void *);       // the function to perform
        void *params;               // parameters to pass to the function
        task_dispatch_type_t type;  // asynchronous or synchronous
        sem_t complete;
    } task_t;
    
    typedef struct dispatch_queue_t dispatch_queue_t; // the dispatch queue type
    typedef struct dispatch_queue_thread_t dispatch_queue_thread_t; // the dispatch queue thread type

    struct dispatch_queue_thread_t {
        dispatch_queue_t *queue;// the queue this thread is associated with
        pthread_t thread;       // the thread which runs the task
        task_t *task;           // the current task for this tread
        sem_t finished;
        sem_t terminate;
    };

    typedef struct node{
        task_t *task;
        struct node* next;
    } node_t;

    struct dispatch_queue_t {
        queue_type_t queue_type;            // the type of queue - serial or concurrent
        dispatch_queue_thread_t* *threads;
        sem_t thread_semaphore; // the semaphore the thread waits on until a task is allocated
        sem_t node_semaphore;
        pthread_mutex_t lock;
        pthread_mutex_t nodeLock;
        node_t *head;
    };

    task_t *task_create(void (*)(void *), void *, char*);
    
    void task_destroy(task_t *);

    dispatch_queue_t *dispatch_queue_create(queue_type_t);
    
    void dispatch_queue_destroy(dispatch_queue_t *);
    
    int dispatch_async(dispatch_queue_t *, task_t *);
    
    int dispatch_sync(dispatch_queue_t *, task_t *);
    
    void dispatch_for(dispatch_queue_t *, long, void (*)(long));
    
    int dispatch_queue_wait(dispatch_queue_t *);

#endif	/* DISPATCHQUEUE_H */