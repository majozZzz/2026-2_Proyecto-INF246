#include "kernel/types.h"
#include "user.h"
#include "threads.h"

struct thread all_thread[MAX_THREADS];
struct thread *current_thread;

// Inicialización
void thread_init(void) {
  for (int i = 0; i < MAX_THREADS; i++) {
    all_thread[i].id = i;
    all_thread[i].state = LIBRE;
  }

  all_thread[0].state = CORRIENDO;
  current_thread = &all_thread[0];
}

static void thread_start(void) {
  current_thread->func();
  thread_exit();
}

// Requerimiento B
// Retorna el id de la hebra, o -1 si no hay espacio.
int thread_create(void (*func)(void)) {
  for (int i = 1; i < MAX_THREADS; i++) {
    struct thread *t = &all_thread[i];
    if (t->state != LIBRE)
      continue;

    memset(&t->ctx, 0, sizeof(t->ctx));
    t->func = func;

    t->ctx.ra = (uint64)thread_start;
    t->ctx.sp = ((uint64)t->stack + STACK_SIZE) & ~0xFUL;

    t->state = LISTA_PARA_CORRER;
    return t->id;
  }
  return -1;
}

// Requerimiento C
void thread_schedule(void) {

}

// Requerimiento D
void thread_yield(void) {
  current_thread->state = LISTA_PARA_CORRER;
  thread_schedule();
}

void thread_exit(void) {
  current_thread->state = LIBRE;
  thread_schedule();
  // no debería volver aquí, esta hebra ya no se vuelve a elegir.
}
