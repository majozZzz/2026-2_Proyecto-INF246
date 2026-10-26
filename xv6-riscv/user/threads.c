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
  struct thread *t_previa = current_thread;
  struct thread *t_siguiente = 0;

  for (int offset = 1; offset <= MAX_THREADS; offset++) {
    int id = (t_previa->id + offset) % MAX_THREADS;
    if (all_thread[id].state == LISTA_PARA_CORRER) {
      t_siguiente = &all_thread[id];
      break;
    }
  }

  if (t_siguiente == 0) {
    if (t_previa->state == CORRIENDO)
      return;
    else if (all_thread[0].state == ESPERANDO)
      t_siguiente = &all_thread[0];
    else
      exit(0);
  }

  t_siguiente->state = CORRIENDO;
  current_thread = t_siguiente;

  if (t_siguiente != t_previa) {
    if (t_previa->state == CORRIENDO)
      t_previa->state = (t_previa->id == 0) ? ESPERANDO : LISTA_PARA_CORRER;
    //esta funcion esta en codigo ensamblador, guarda el contexto actual y carga el siguiente
    thread_switch(&t_previa->ctx, &t_siguiente->ctx);
  }
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
