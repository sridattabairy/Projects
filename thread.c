/***********************************************
 *											   *	
 * Non pre-emptive user level thread Library.  *
 *											   *	
 **********************************************/

#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define STACK_SIZE  1024*8 
#define true 1
#define false 0

static ucontext_t main_context;        /* Main thread context */
static ucontext_t process_context;     /* Process context */ 
static unsigned int tid = 1;           /* id for thread identification */
static unsigned int current_tid;       /* Running thread id */
static short yield = false;	       	   /* Value is set if MyThreadYield is called by the user */
static short join = false;	           /* Value is set if MyThreadJoin is called by the user */
static short release = false;          /* Value is set if thread is removed from the blockQ */
static unsigned int blockid;           /* thread id of the removed thread from blockQ */
static short sem_block = false;	       /* Value is set if thread is waiting for a semaphore */	


/* Semaphore */

struct semaphore {
int value;
};



struct Queue {
	ucontext_t *context; 		/* thread context */
	struct Queue *next;  		/* Pointer to the next element in the Queue */
	short isblocked;     		/* Set if thread is blocked */ 
	short isblockedbysemaphore; /* Set if thread is blocked for a semaphore */
	unsigned int tid;			/* thread id */
};


/* Thread pool structure */

struct list {
unsigned int tid;  		/* thread id */
unsigned int pid;		/* Parent id */
ucontext_t *context;	/* Pointer to the thread context */
short invalid;			/* Set if thread has completed its execution */
short isblocking;		/* Set if thread is blocking the parent */
int join;				/* Number of threads blocking the parent */			
struct list *next;		/* Pointer to the next element in the thread pool */
};

struct list *threadlist;   		/* Head of thread pool */ 
struct Queue *readyQ;			/* Head of readyQ */
struct Queue *blockQ;			/* Head of blockQ */
struct Queue *rlast, *blast;	/* Last entry in the readyQ and blockQ respectively */


void insert(ucontext_t *context);


/* 
 ** This function will be called by MyThreadInit to run the main thread.
 ** Once main thread completes, next available thread in the readyQ is 
 ** picked up for execution.
 */

static void MyThreadRun(void) {

	struct Queue *first = NULL;
	struct Queue *blk = NULL;
	struct Queue *tmp = NULL;
	struct Queue *prev = NULL;
	struct list *tlistfree = NULL;
	short rel = false;
	ucontext_t *context = NULL;

	/* Add mainthread entry into the threadlist */

	threadlist = (struct list *)malloc(sizeof(struct list));

	if ( threadlist == NULL ) {
		printf("memory allocation failed\n");
		exit(0);
	}


	memset(threadlist, 0, sizeof(struct list));

	/* Add an entry in the pool for the main thread */

	threadlist->context = &main_context;
	threadlist->tid = tid;
	threadlist->pid = 0;
	threadlist->next = NULL;
	current_tid = tid;
	tid++;

	/* Run the main thread by setting the context */
	swapcontext(&process_context, &main_context);

	/* Iterate till there are no entries in both readyQ and blockQ */

	while(readyQ != NULL || blockQ != NULL) {

	/* 
	 ** Check if there are any entries in the blockQ.
	 ** if yes then move it to the readyQ if any thread
	 ** is unblocked.
	 */

	tmp = blockQ;
	prev = NULL;
	rel = false;


	while(tmp) {

		/* Check if any thread is unblocked (by semaphore and by child). */

		if(!(tmp->isblocked) && !(tmp->isblockedbysemaphore)) {
			rel = true;
			break;
		}
		prev = tmp;
		tmp = tmp->next;
	}

		/* If thread is unblocked move it to readyQ for execution */

		if(rel) {
			release = true;
			blockid = tmp->tid;
			if(tmp == blockQ) {
				insert(blockQ->context);
				blockQ=blockQ->next;
				free(tmp);
			}
			else {
			prev->next = tmp->next;
			insert(tmp->context);
			free(tmp);
			}
		}

		/* set the context to first entry of the readyQ if exists */

		if(readyQ) {
			first = readyQ;
			context = first->context;
			readyQ = readyQ->next;
			current_tid = first->tid;
			free(first);
			setcontext(context);
		}
	}

	/* Free the thread pool and its associated context */

	while(threadlist) {
		tlistfree = threadlist;
		free(tlistfree->context->uc_stack.ss_sp);  /* Free associated stack for the thread */
		if(tlistfree->tid != 1)
		free(tlistfree->context);		   		  /* Free the thread context */
		threadlist = threadlist->next;
		free(tlistfree);			   			 /* Free the thread pool entries */
	}
}



/* 
 ** Function to insert the context into the readyQ.
 */ 

void insert(ucontext_t *context) {

	struct Queue *new=(struct Queue *)malloc(sizeof(struct Queue));
	struct Queue *temp = NULL;

	if (new == NULL) {
	printf("memory allocation failed\n");
	exit(0);
	}

	memset(new, 0, sizeof(struct Queue));

	/* Check if the insertion is for readyQ or blockQ */

	if(yield || join || sem_block) {  
		new->tid = current_tid;
		yield = false;
	}
	else if(release) {
		release = false;
		new->tid = blockid;
	}
	else {
		new->tid = tid;
		tid++;
	}

	new->context = context;
	new->isblocked = false;
	new->next = NULL;

	if((!join) && (!sem_block)) { 
		temp = readyQ;
	}
	else {
		temp = blockQ;
		if(join)
			new->isblocked = true;
		else
			new->isblockedbysemaphore = true;
	}

	/* Check if readyQ or blockQ is empty */

	if(!temp) {
		if((!join) && (!sem_block)) {
			readyQ = new;
			rlast = new;
		}
		else {
			blockQ = new;
			blast = new;
		}
	}
	else {
		if((!join) && (!sem_block)) {
			rlast->next = new;
			rlast = new;
		}
	else {
		blast->next = new;
		blast = new;
		}
	}
	join = false;
	sem_block = false;
}


/* 
 ** Function to create the main thread.
 ** This function should be called only once by unix process. 
 */   


void MyThreadInit (void(*start_funct)(void *), void *args) {

	getcontext(&main_context);
	main_context.uc_link = &process_context;
	main_context.uc_stack.ss_sp = malloc(STACK_SIZE);

	if ( main_context.uc_stack.ss_sp == NULL ) {
		printf("memory allocation failed\n");
		exit(0);
	}

	main_context.uc_stack.ss_size = STACK_SIZE;

	/* Create main thread */

	makecontext(&main_context, (void *)start_funct,1,args);

	MyThreadRun();

}



/*
 ** Function to create and schedule user threads.
 */

void *MyThreadCreate (void(*start_funct)(void *), void *args) {

	ucontext_t *thread_context = malloc(sizeof(ucontext_t));
	struct list *tlist = (struct list *)malloc(sizeof(struct list));
	struct list *tlast = threadlist;

	if ( thread_context == NULL ) {
		printf("memory allocation failed\n");
		exit(0);
	}

	if ( tlist == NULL ) {
		printf("memory allocation failed\n");
		exit(0);
	}

	/* Initialize */

	memset(tlist, 0, sizeof(struct list));
	memset(thread_context, 0, sizeof(ucontext_t));

	getcontext(thread_context);
	thread_context->uc_link = &process_context;
	thread_context->uc_stack.ss_sp = malloc(STACK_SIZE);

	if ( thread_context->uc_stack.ss_sp == NULL ) {
		printf("memory allocation failed\n");
		exit(0);
	}

	thread_context->uc_stack.ss_size = STACK_SIZE;

	/* Create a thread */

	makecontext(thread_context, (void *)start_funct,1,args);

	/* Add an entry into the threadlist */
	 
	tlist->context = thread_context; 
	tlist->tid = tid;
	tlist->pid = current_tid;
	tlist->next = NULL;

	while(tlast->next) {
		tlast = tlast->next;
	}
	tlast->next = tlist;

	/* Insert the thread context into the readyQ */

	insert(thread_context);

	return tlist;
}



/*
 ** Function suspends execution of invoking thread 
 ** and yields to another thread in the readyQ.
 */

void MyThreadYield(void) {

	struct Queue *first = NULL;
	struct list *tlist = threadlist;

	while(tlist) {
		if(current_tid == tlist->tid)
			break;
		tlist = tlist->next;
	}

	if((readyQ) && (tlist)) {
		getcontext(tlist->context);
		yield = true;
		insert(tlist->context);
		first = readyQ;
		readyQ = readyQ->next;
		current_tid = first->tid;
		swapcontext(tlist->context,first->context);
	}
}

/* 
 ** Function to terminate the thread.
 */

void MyThreadExit(void) {

	struct Queue *blk = NULL;
	unsigned int pid;
	struct list *tlist = threadlist;
	struct list *tplist = threadlist;

	blk=blockQ;

	/* Get the pointer to the current list */

	while(tlist) {
		if(tlist->tid == current_tid) 
			break;
		tlist = tlist->next;
	}

	/* Get the parent list */

	while(tplist) {
		if(tplist->tid == tlist->pid) 
			break;
		tplist = tplist->next;
	}

	/* 
	 ** Unblock the parent if it is the only child or
	 ** just decrement the block counter to account
	 ** for the current child.
	 */

	if(tlist->isblocking) {
		tplist->join--;

		while(blk != NULL) {
			if(((blk->tid) == (tlist->pid)) && ((tplist->join) == 0)) {
			blk->isblocked = false;
				break;
			}
			blk=blk->next;
		}
	}

	tlist->isblocking = 0;
	tlist->invalid = 1;
	setcontext(&process_context);
}


/*
 ** Joins the invoking function with the specified child thread.
 ** Returns 0 on success (after any necessary blocking). 
 ** Returns -1 on failure.
 */


int MyThreadJoin(struct list *thread) {

	int success = 0;
	struct list *tlist = threadlist;

	/* 
	 ** Check whether thread is the immediate child of the current context.
	 */

	if(!thread) return -1; 

	if((current_tid == thread->pid) && (!(thread->invalid))) {
		join = true;
		while(tlist) {
			if(tlist->tid == current_tid) 
			break;
			tlist = tlist->next;
		}

		tlist->join++;
		thread->isblocking = true;
		getcontext(tlist->context);
		insert(tlist->context);
		success = true;

		/* Block the thread */
		swapcontext(tlist->context,&process_context);
	}

	if(success) return 0;
	return -1;

}

/*
 ** This function waits till all children have terminated. 
 ** Returns immediately if there are no active children. 
 */

void MyThreadJoinAll(void) {

	struct list *tlist = threadlist;
	struct list *tclist = threadlist;


	/* 
	 ** Check whether the current thread has any children and if yes 
	 ** move the current thread to the blocked Q.
	 */

	/* Get the current thread */

	while(tclist) {
		if(tclist->tid == current_tid) 
			break;
		tclist = tclist->next;
	}

	while(tlist) {
		if((current_tid == tlist->pid) && (!(tlist->invalid))) {
			join = true;
			tlist->isblocking = true;
			tclist->join++;
		}
		tlist = tlist->next;
	}

	if(join) {
		getcontext(tclist->context);
		insert(tclist->context);
		swapcontext(tclist->context,&process_context);
	}

}

/*
 ** Create a semaphore. Set the initial value to initialValue.
 */

struct semaphore* MySemaphoreInit(int initialValue) {

	struct semaphore *sem = (struct semaphore *)malloc(sizeof(struct semaphore));

	if ( sem == NULL ) {
		printf("memory allocation failed\n");
		exit(0);
	}

	sem->value = initialValue;
	return sem;
}


/*
 ** Signal the semaphore. The invoking thread is not pre-empted.
 */ 

void MySemaphoreSignal(struct semaphore *sem) {

	struct Queue *blk = blockQ;

	sem->value++;

	if(sem->value<=0) {
		while(blk != NULL) {
			if(blk->isblockedbysemaphore) {
			blk->isblockedbysemaphore = false;
				break;
			}
		blk=blk->next;
		}
	}
}


/*
 ** Wait on semaphore by moving the thread to blockQ
 */

void MySemaphoreWait(struct semaphore *sem) {

	struct list *tclist = threadlist;

	while(tclist) {
		if(tclist->tid == current_tid) 
			break;
		tclist = tclist->next;
	}

	sem->value--;
	if(sem->value<0) {
		sem_block = true;
		getcontext(tclist->context);
		insert(tclist->context);
		swapcontext(tclist->context,&process_context);
	}
}


/*
 ** Destroy semaphore sem. Do not destroy semaphore if any threads 
 ** are blocked on the queue. Return 0 on success, -1 on failure. 
 */


int MySemaphoreDestroy(struct semaphore *sem) {

	struct Queue *blk = blockQ;
	int tobefreed = true;

	while(blk) {
		if(blk->isblockedbysemaphore) {
			tobefreed = false;
			break;
		}
		blk=blk->next;
	}

	if(tobefreed) {
		free(sem);
		return 0;
	}

	return -1;

}

