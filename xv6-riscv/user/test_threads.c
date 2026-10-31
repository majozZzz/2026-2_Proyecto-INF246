#include "kernel/types.h"
#include "user.h"
#include "threads.h"

int numero_global = 0;

static void thread_work(void) {
  int id = current_thread->id;

  for (int i = 0; i < 5; i++) {
    numero_global += 5;
    thread_yield();
    numero_global -= 2;
    printf("Hebra %d: %d\n", id, numero_global);
  }
  thread_exit();
}

int main(void) {
  thread_init();
  int n = 3;
  for (int i = 0; i < n; i++){
    thread_create(thread_work);
  }

  thread_schedule();

  printf("\ntest completado\n");
  exit(0);
}
