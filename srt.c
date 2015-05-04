/*
 * srt.c
 *
 * Created: 2015-01-01 18:05:19
 *  Author: Lenovo
 */

#include <setjmp.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include "srt.h"

#define YES				      1
#define NULL            0
#define NO				      NULL
#define DISABLE()       cli()
#define ENABLE()        sei()
#define STACKSIZE       10			// 80 original
#define NTHREADS        4			// 4 original - specify how many threads you'll create
#define SETSTACK(buf,a) *((unsigned int *)(buf)+8) = (unsigned int)(a) + STACKSIZE - 4; \
*((unsigned int *)(buf)+9) = (unsigned int)(a) + STACKSIZE - 4


struct thread_block {
	uint8_t delay;						// delay, number of interrupts
	uint8_t is_main;
	uint8_t priority;					// if other thread was dispatched in this threads place
	void (*function)(int/*, more areguments*/);				// code to run
	uint8_t arg;						// argument to the above
	thread next;						// for use in linked lists
	jmp_buf context;					// machine state
	uint8_t stack[STACKSIZE];			// execution stack space
};

static struct thread_block threads[NTHREADS];

static struct thread_block initp = { NO, YES, NULL }; // default thread is not delayed, is main and no (low) priority

static thread freeQ   = threads;
static thread readyQ  = NULL;
static thread current = &initp;

static uint8_t initialized = NO;

static void initialize(void) {
	for (uint8_t i = 0; i < NTHREADS - 1; i++)
	threads[i].next = &threads[i + 1];
	threads[NTHREADS - 1].next = NULL;
	initialized = 1;
}

static void enqueue(thread p, thread *queue) {
	p->next = NULL;
	if (!*queue) {
		*queue = p;
	} else {
		thread q = *queue;
		while (q->next) q = q->next;
		q->next= p;
	}
}

static thread dequeue(thread *queue) {
	thread p = *queue;
	if (*queue) {
		*queue = (*queue)->next;
	} else {
		// Empty queue, kernel panic!!!
		WDTCSR = (1 << WDCE) ;
		WDTCSR = (0 << WDIE) | (0 << WDE) ;
		while (1) {
			MCUCR = (1 << BODS) | (1 << BODSE) ;
			MCUCR |= (1 << BODS) ;
			MCUCR &= ~(1 << BODSE) ;
			sleep_cpu();
		}
	}
	return p;
}

static void dispatch(thread next) {
	if (!setjmp(current->context)) {
		current = next;
		longjmp(next->context, 1);
	}
}

static void spawndispatch(thread *queue) {
	thread q = *queue;
	thread prio = NULL;
	while (q) {
		if (!q->delay) {
			if (prio) {
				if ((q->priority > prio->priority)) prio = q;
			} else {
				prio = q;
			}
		}
		q = q->next;
	}
	thread disp = dequeue(queue);
	while (disp != prio) {
		enqueue(disp, queue);
		disp = dequeue(queue);
	}
	dispatch(disp);
}

/*static void rec(thread *queue) {
	thread q = dequeue(queue);
	if (q->delay) {
		enqueue(q, queue);
		rec(queue);
	} else {
		dispatch(q);
	}
}*/

void spawn(void function(int), uint8_t arg, uint8_t delay) {
	thread newp;
	DISABLE();
	if (!initialized) initialize();
	newp = dequeue(&freeQ);
	newp->function = function;
	newp->arg = arg;
	newp->delay = delay;
	newp->is_main = NO;
	newp->priority = 1;
	newp->next = NULL;
	if (setjmp(newp->context)) {
		ENABLE();
		current->function(current->arg);
		DISABLE();
		enqueue(current, &freeQ);
		spawndispatch(&readyQ);
	}
	SETSTACK(&newp->context, &newp->stack);
	enqueue(newp, &readyQ);
	ENABLE();
}

static void scheduler(thread *queue) {
	current->priority = current->is_main ? 0 : 1 ;
	thread q = *queue;
	thread prio = current;
	while (q) {
		q->delay -= q->delay ? 1 : 0 ;
		if (!q->delay) {
			if (q->priority > prio->priority) {
				prio->priority++;
				prio = q;
			} else {
				q->priority++;
			}
		}
		q = q->next;
	}
	if (prio != current) {
		thread disp = current;
		while (disp != prio) {
			enqueue(disp, queue);
			disp = dequeue(queue);
		}
		dispatch(disp);
	}
}

void lock(mutex *m) {
	DISABLE();
	while (m->locked) {
		enqueue(current, &(m->waitQ));
		dispatch(dequeue(&readyQ));
	}
	m->locked = LOCKED;
	ENABLE();
}

void unlock(mutex *m) {
	DISABLE();
	while (m->waitQ) {
		enqueue(current, &readyQ);
		dispatch(dequeue(&(m->waitQ)));
	}
	m->locked = UNLOCKED;
	ENABLE();
}

ISR(WDT_vect) {
	DISABLE();
	scheduler(&readyQ);
	ENABLE();
}
