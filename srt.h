/*
 *
 * srt.h
 *
 */

#ifndef _SRT_H
#define _SRT_H

#include <avr/io.h>

struct thread_block;
typedef struct thread_block *thread;
void spawn(void (*code)(int), uint8_t arg, uint8_t delay);

struct mutex_block {
    uint8_t locked;
    thread waitQ;
};
typedef struct mutex_block mutex;

#define LOCKED		1
#define UNLOCKED	0
#define MUTEX_INIT	{ UNLOCKED, UNLOCKED }
void lock(mutex *m);
void unlock(mutex *m);

#endif
