#include "dispatchQueue.h"

node_t *createNode(task_t *task, node_t *next){
    //allocate nodes onto the heap so that they persist past this function scope.
    //MAKE SURE THIS IS FREED LATER
    node_t *new_node = (node_t*) malloc(sizeof(node_t));
    if(new_node == NULL){
        printf("Error creating a new node\n");
        exit(0);
    }
    
    new_node->task = task;
    new_node->next = next;
    return new_node;
}

//add the first node to the queue
node_t *prepend(node_t *head, task_t *task){
    node_t *new_node = createNode(task, head);
    head = new_node;
    return head;
}

//remove the first node of the list, and redirect the header pointer.
node_t *popNode(dispatch_queue_t* dispatchQueue){
    pthread_mutex_lock(&dispatchQueue->nodeLock);
    if(dispatchQueue->head == NULL){
        //list is empty
        pthread_mutex_unlock(&dispatchQueue->nodeLock);
        return NULL;
    }

    //pass in pointer to dispatch_queue_t rather than head pointer directly due to
    //passing parameters by value which means its copied and the heap header is not modified
    node_t *front = dispatchQueue->head;
    dispatchQueue->head = dispatchQueue->head->next;
    front->next = NULL;
    if(front == dispatchQueue->head){
        dispatchQueue->head = NULL;
    }
    pthread_mutex_unlock(&dispatchQueue->nodeLock);
    return front;
}

void traverse(node_t* head){
    node_t *cursor = head;
    while(cursor->next != NULL){
        cursor = cursor->next;
    }
}

//add a node to the end of the queue
node_t *appendNode(dispatch_queue_t *queue, task_t *next){
    //node_semaphore guaranteees that only one thread is modifying the list at the same time.
    sem_wait(&queue->node_semaphore);

    //lock to prevent multiple list access incase semaphore is violated.
    pthread_mutex_lock(&queue->nodeLock);

    //if the list being added to is empty call prepend.
    if(queue->head == NULL){
        queue->head = prepend(queue->head, next);
        #ifdef DEBUG
            printf("prepend set the head to %p\n", queue->head);
        #endif
        sem_post(&queue->node_semaphore);
        pthread_mutex_unlock(&queue->nodeLock);
        return queue->head;
    }

    //append to non-empty list
    node_t *cursor = queue->head;
    while(cursor->next != NULL){
        cursor = cursor->next;
    }
    node_t *new_node = createNode(next, NULL);
    cursor->next = new_node;
    traverse(queue->head);

    //free lock
    pthread_mutex_unlock(&queue->nodeLock);

    //free semaphore
    sem_post(&queue->node_semaphore);
    return queue->head;
}

task_t *task_create(void (* func)(void *), void *params, char *name){
    //allocate task on heap for persistance
    task_t *task = (task_t*) malloc(sizeof(task_t));
    task->work = func;
    strcpy(task->name, name);
    task->params = params;

    //initialise the complete semaphore. dispatch_sync will wait on the associated tasks complete semaphore.
    sem_init(&task->complete, 0, 0);
    return task;
}

void task_destroy(task_t *taskPtr){
    free(taskPtr);
}

void* start_pthread(void *args){
    #ifdef DEBUG
        printf("thread spinning\n");
    #endif
    dispatch_queue_thread_t* queueThreadPtr = (struct dispatch_queue_thread_t*)args;
    #ifdef DEBUG
    printf("starting pthread with dispatch queue at address %p on thread %p", queueThreadPtr->queue, queueThreadPtr);
    #endif
    //infinite loop so that the thread continuously checks for work to complete at sem_wait
    int on = 1;
    while(on){
        //semaphore which controls access to processing tasks.
        //posted to when a dispatch method has been called to indicate tasks are available for processing.
        sem_wait(&queueThreadPtr->queue->thread_semaphore);

        #ifdef DEBUG
            printf("passed semaphore on thread %p\n", queueThreadPtr);
        #endif

        //lock simultaneous access to popping the linkedlist
        pthread_mutex_lock(&queueThreadPtr->queue->lock);

        node_t *poppedNode = popNode(queueThreadPtr->queue);

        if(poppedNode == NULL){
            //if the list is empty this indicates a dispatch_queue_wait call
            //the dispatch_queue_wait has simulated that tasks are available
            #ifdef DEBUG
                printf("linked list head was set to null\n");
            #endif
            pthread_mutex_unlock(&queueThreadPtr->queue->lock);
            //break the working infinite loop and enter into the next wait pending a
            //dispatch_queue_destroy call to terminate the threads.
            break;
        }
    
        #ifdef DEBUG
            printf("head of linked list pointer is %p\n", queueThreadPtr->queue->head);
        #endif

        pthread_mutex_unlock(&queueThreadPtr->queue->lock);

        #ifdef DEBUG
            printf("prefunction invocation\n");
            printf("work function at location %p\n", poppedNode->task);
        #endif

        #ifdef DEBUG
            printf("work dispatched on thread %p\n", queueThreadPtr);
        #endif

        //execute the work function
        poppedNode->task->work(poppedNode->task->params);

        //post that the work has been completed in the task.
        sem_post(&poppedNode->task->complete);

        #ifdef DEBUG
            printf("work function invoked\n");
        #endif

        task_destroy(poppedNode->task);

        free(poppedNode);

    }
    #ifdef DEBUG
        printf("infinite loop broken on thread %p\n", queueThreadPtr);
    #endif
    sem_post(&queueThreadPtr->finished);

    //this semaphore will pass only when a dispatch_queue_destroy is called
    sem_wait(&queueThreadPtr->terminate);
    pthread_exit(NULL);
    return NULL;
}

dispatch_queue_t *dispatch_queue_create(queue_type_t queueType){
    #ifdef DEBUG
        printf("dispatch queue create called\n");
    #endif

    //number of processors that defines the number of threads to create per queue;
    int numOfProcessors = get_nprocs();
    dispatch_queue_t *dispatchQueue = (dispatch_queue_t*) malloc(sizeof(dispatch_queue_t));
    //initialise head of linked list of tasks to null;
    dispatchQueue->head = NULL;
    pthread_mutex_init(&dispatchQueue->lock, NULL);
    pthread_mutex_init(&dispatchQueue->nodeLock, NULL);
    dispatchQueue->queue_type = queueType;

    if(queueType == SERIAL){
        numOfProcessors = 1;
    }

    sem_init(&dispatchQueue->thread_semaphore, 0, 0);
    sem_init(&dispatchQueue->node_semaphore,0 ,1);

    dispatchQueue->threads = (dispatch_queue_thread_t**) malloc(sizeof(dispatch_queue_thread_t*) * numOfProcessors);
    
    for(int i = 0; i < numOfProcessors; i++){
        #ifdef DEBUG
            printf("pthread initialisaiton loop entered\n");
        #endif
        dispatch_queue_thread_t *dispatchThread = (dispatch_queue_thread_t*) malloc(sizeof(dispatch_queue_thread_t));
        #ifdef DEBUG
            printf("dispatch queue thread created at address %p\n", dispatchThread);
        #endif
        sem_init(&dispatchThread->finished, 0 ,0);
        sem_init(&dispatchThread->terminate, 0, 0);
        dispatchQueue->threads[i] = dispatchThread;
        dispatchThread->queue = dispatchQueue;

        //pthread_create allocates the pthread into memory AND STARTS THE THREAD IMMEDIATELY
        pthread_create(&dispatchThread->thread, NULL, start_pthread, dispatchThread);
    }
    #ifdef DEBUG
        printf("created dispatch queue at address %p\n", dispatchQueue);
    #endif
    return dispatchQueue;
}
    
void dispatch_queue_destroy(dispatch_queue_t *dispatchQueuePtr){
    //terminate the threads by posting to their terminate semaphore.
    if(dispatchQueuePtr->queue_type == CONCURRENT){
        for(int i= 0; i < get_nprocs(); i++){
            sem_post(&dispatchQueuePtr->threads[i]->terminate);
        }
    }else{
        sem_post(&dispatchQueuePtr->threads[0]->terminate);
    }

    //if the linkedlist has had nodes added to it after dispatch_queue_wait has been called
    //free the memory associated with the linked list by traversing it.
    if(dispatchQueuePtr->head != NULL){
        node_t *cursor = dispatchQueuePtr->head;
        while(cursor->next != NULL){
            node_t *previous = cursor;
            cursor = cursor->next;
            free(previous);
        }
    }

    //free the memory associated with the dispatch_queue_thread_t mallocs and then the
    //dispatch_queue malloc itself.
    if(dispatchQueuePtr->queue_type == CONCURRENT){
        for(int i = 0; i < get_nprocs(); i++){
            free((dispatchQueuePtr->threads[i]));
        }
    }else{
        free(dispatchQueuePtr->threads[0]);
    }
    free(dispatchQueuePtr);
}
    
int dispatch_async(dispatch_queue_t *dispatchQueuePtr, task_t *taskPtr){
    #ifdef DEBUG
        printf("dispatch async called\n");
    #endif

    //add task to the linked list
    appendNode(dispatchQueuePtr, taskPtr);

    //indicate that work has been added so that the pthreads that are waiting on this semaphore can continue.
    sem_post(&dispatchQueuePtr->thread_semaphore);
    #ifdef DEBUG
        printf("sem posted from node \n");
    #endif
}
    
int dispatch_sync(dispatch_queue_t *dispatchQueuePtr, task_t *taskPtr){
    #ifdef DEBUG
        printf("dispatch sync called\n");
    #endif
    appendNode(dispatchQueuePtr, taskPtr);
    #ifdef DEBUG
        printf("post append\n");
    #endif
    sem_post(&dispatchQueuePtr->thread_semaphore);

    //wait until the task has complete which is posted to my the pthread after the work execution.
    sem_wait(&taskPtr->complete);
}
    
void dispatch_for(dispatch_queue_t *dispatchQueue, long num, void (*func)(long)){
    for(int i = 0; i < num; i++){
        char name = i + '0';
        char* nameAsPointer = &name;
        task_t *task = task_create((void *)func,(void *) i, nameAsPointer);
        dispatch_async(dispatchQueue, task);
    }
    dispatch_queue_wait(dispatchQueue);
}
    
int dispatch_queue_wait(dispatch_queue_t *dispatchQueue){
    #ifdef DEBUG
        printf("wait is called\n");
    #endif
    //block all other threads from accessing the list.
    sem_wait(&dispatchQueue->node_semaphore);
    //have all the threads continue from sem_wait. List should be empty and hence the threads will exit the infinite loop.
    for(int i = 0; i < get_nprocs(); i++){
        sem_post(&dispatchQueue->thread_semaphore);
        #ifdef DEBUG
            printf("sem posted\n");
        #endif
    }

    //check that the threads have all successfully exited their infinite loops by waiting on the finished semaphore
    if(dispatchQueue->queue_type == CONCURRENT){
        for(int i = 0; i < get_nprocs(); i++){
            sem_wait(&dispatchQueue->threads[i]->finished);
            #ifdef DEBUG
            printf("thread %d terminated\n", i);
            #endif
        }
    }else{
        sem_wait(&dispatchQueue->threads[0]->finished);
    }
    #ifdef DEBUG
        printf("wait returns\n");
    #endif
    return 1;
}

