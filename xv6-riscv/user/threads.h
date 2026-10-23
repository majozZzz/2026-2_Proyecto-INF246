#ifndef THREADS_H
#define THREADS_H

#include "kernel/types.h"

#define MAX_THREADS 8
#define STACK_SIZE  4096

enum thread_state {
  LIBRE = 0,
  LISTA_PARA_CORRER,
  CORRIENDO
};

struct context {
  uint64 ra;
  uint64 sp;
  uint64 s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
};

struct thread {
  int id;
  enum thread_state state;
  void (*func)(void);
  struct context ctx;
  char stack[STACK_SIZE] __attribute__((aligned(16)));
};

extern struct thread all_thread[MAX_THREADS];
extern struct thread *current_thread;

void thread_init(void);
int  thread_create(void (*func)(void));
void thread_schedule(void);
void thread_yield(void);
void thread_exit(void);

extern void thread_switch(struct context *from, struct context *to);

#endif
