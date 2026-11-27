// cowtest: pruebas propias para Copy-on-Write en xv6.
//
// Uso (dentro de xv6):  $ cowtest
// Si todo funciona imprime: ALL COW TESTS PASSED
//
// Pruebas:
//   simple     fork de un proceso con > 50% de la RAM (si fork copia, no cabe)
//   three      3 hijos escriben cada pagina de una region compartida
//   many       20 hijos vivos a la vez compartiendo la misma region
//   isolation  escrituras del hijo no las ve el padre y viceversa
//   copyout    read() del kernel hacia paginas compartidas (pipe y archivo)
//   refcount   la memoria no se libera mientras algun proceso la use
//   oom        agotar la memoria durante un fallo COW no debe causar panic
//   leak       muchos fork+exit no deben filtrar paginas

#include "kernel/types.h"
#include "kernel/memlayout.h"
#include "user/user.h"

#define PG 4096
#define RAM (PHYSTOP - KERNBASE)

static const char *cur = "";

static void
fail(const char *msg)
{
  printf("cowtest: FALLO en '%s': %s\n", cur, msg);
  exit(1);
}

// escribe un valor distinto en el primer byte de cada pagina.
static void
fill(char *p, int sz, int seed)
{
  for (int i = 0; i < sz; i += PG)
    p[i] = (char)(seed + i / PG);
}

static int
check(char *p, int sz, int seed)
{
  for (int i = 0; i < sz; i += PG)
    if (p[i] != (char)(seed + i / PG))
      return 0;
  return 1;
}

static int
waitstatus(void)
{
  int st = 0;
  if (wait(&st) < 0)
    fail("wait");
  return st;
}

static void
test_simple(void)
{
  int sz = RAM / 3 * 2;
  char *p = sbrk(sz);
  if (p == SBRK_ERROR)
    fail("sbrk");
  fill(p, sz, 1);

  int pid = fork();
  if (pid < 0)
    fail("fork sin memoria: fork esta copiando las paginas");
  if (pid == 0)
    exit(check(p, sz, 1) ? 0 : 1);
  if (waitstatus() != 0)
    fail("el hijo vio datos distintos a los del padre");
  if (!check(p, sz, 1))
    fail("el padre perdio sus datos");
  sbrk(-sz);
}

// nchild hijos escriben todas las paginas de una region de sz bytes.
static void
test_many(int nchild, int sz)
{
  char *p = sbrk(sz);
  if (p == SBRK_ERROR)
    fail("sbrk");
  fill(p, sz, 7);

  for (int n = 0; n < nchild; n++) {
    int pid = fork();
    if (pid < 0)
      fail("fork");
    if (pid == 0) {
      if (!check(p, sz, 7))
        exit(1);
      fill(p, sz, 100 + n); // fuerza la copia de cada pagina
      pause(2);             // los hermanos escriben al mismo tiempo
      exit(check(p, sz, 100 + n) ? 0 : 2);
    }
  }
  for (int n = 0; n < nchild; n++)
    if (waitstatus() != 0)
      fail("un hijo vio datos corruptos");
  if (!check(p, sz, 7))
    fail("el padre vio escrituras de sus hijos");
  sbrk(-sz);
}

static volatile int g = 111;

static void
test_isolation(void)
{
  volatile int x = 1;
  volatile char *h = (volatile char *)malloc(16);
  int p2c[2], c2p[2];
  char c;

  if (h == 0)
    fail("malloc");
  h[0] = 'a';
  if (pipe(p2c) < 0 || pipe(c2p) < 0)
    fail("pipe");

  int pid = fork();
  if (pid < 0)
    fail("fork");
  if (pid == 0) {
    close(p2c[1]);
    close(c2p[0]);
    g = 222;
    x = 222;
    h[0] = 'c';
    write(c2p[1], "x", 1);
    read(p2c[0], &c, 1); // espera a que el padre escriba
    exit((g == 222 && x == 222 && h[0] == 'c') ? 0 : 1);
  }
  close(p2c[0]);
  close(c2p[1]);
  if (read(c2p[0], &c, 1) != 1)
    fail("pipe");
  if (g != 111 || x != 1 || h[0] != 'a')
    fail("el padre vio escrituras del hijo");
  g = 333;
  x = 333;
  h[0] = 'p';
  write(p2c[1], "x", 1);
  if (waitstatus() != 0)
    fail("el hijo vio escrituras del padre");
  if (g != 333 || x != 333 || h[0] != 'p')
    fail("el padre perdio sus escrituras");
  close(p2c[1]);
  close(c2p[0]);
  free((void *)h);
}

// buffer en BSS, alineado a pagina: se comparte con el hijo tras fork.
static char buf[3 * PG] __attribute__((aligned(PG)));

static int
allp(void)
{
  for (int i = 0; i < (int)sizeof(buf); i++)
    if (buf[i] != 'P')
      return 0;
  return 1;
}

static void
test_copyout(void)
{
  int fd[2];
  char data[1000];

  // 1) read() desde un pipe hacia una pagina compartida (cruza 2 paginas)
  memset(buf, 'P', sizeof(buf));
  if (pipe(fd) < 0)
    fail("pipe");
  int pid = fork();
  if (pid < 0)
    fail("fork");
  if (pid == 0) {
    close(fd[1]);
    int n = 0;
    while (n < 6000) {
      int m = read(fd[0], buf + 100 + n, 6000 - n);
      if (m <= 0)
        exit(1);
      n += m;
    }
    for (int i = 0; i < 6000; i++)
      if (buf[100 + i] != 'C')
        exit(2);
    if (buf[99] != 'P' || buf[6100] != 'P')
      exit(3);
    exit(0);
  }
  close(fd[0]);
  memset(data, 'C', sizeof(data));
  for (int i = 0; i < 6; i++)
    write(fd[1], data, sizeof(data));
  close(fd[1]);
  if (waitstatus() != 0)
    fail("read() de pipe sobre pagina COW fallo en el hijo");
  if (!allp())
    fail("read() del hijo modifico la memoria del padre");

  // 2) read() desde un archivo hacia una pagina compartida
  int f = open("README", 0);
  if (f < 0)
    fail("open README");
  memset(buf, 'P', sizeof(buf));
  pid = fork();
  if (pid < 0)
    fail("fork");
  if (pid == 0) {
    int n = read(f, buf + PG - 10, 200); // cruza el limite de pagina
    exit((n > 0 && buf[PG - 10] != 'P') ? 0 : 1);
  }
  if (waitstatus() != 0)
    fail("read() de archivo sobre pagina COW fallo en el hijo");
  if (!allp())
    fail("read() del hijo modifico la memoria del padre");
  close(f);
}

// el hijo C crea a G y termina antes que G: la memoria compartida debe
// seguir viva para G (el contador de referencias no puede llegar a 0).
static void
test_refcount(void)
{
  int sz = 4 * PG;
  char *p = sbrk(sz);
  int fd[2];
  char r = 0;

  if (p == SBRK_ERROR)
    fail("sbrk");
  fill(p, sz, 50);
  if (pipe(fd) < 0)
    fail("pipe");

  int pid = fork();
  if (pid < 0)
    fail("fork");
  if (pid == 0) { // C
    int pid2 = fork();
    if (pid2 < 0)
      exit(1);
    if (pid2 == 0) { // G
      close(fd[0]);
      pause(5); // C ya termino
      char res = check(p, sz, 50) ? 'K' : 'E';
      fill(p, sz, 80); // escribe despues de que C solto sus referencias
      if (!check(p, sz, 80))
        res = 'W';
      write(fd[1], &res, 1);
      exit(0);
    }
    exit(0); // C termina antes que G
  }
  close(fd[1]);
  if (waitstatus() != 0)
    fail("el hijo fallo");
  fill(p, sz, 60); // el padre tambien escribe
  if (read(fd[0], &r, 1) != 1)
    fail("el nieto murio (pagina liberada antes de tiempo?)");
  if (r != 'K')
    fail("el nieto vio memoria liberada o corrupta");
  if (!check(p, sz, 60))
    fail("el padre perdio sus datos");
  close(fd[0]);
  sbrk(-sz);
}

// el hijo intenta copiar mas memoria de la que hay: el kernel debe matarlo
// (verá "usertrap(): unexpected scause" en consola; es lo esperado) sin
// entrar en panic, y el padre debe seguir intacto.
static void
test_oom(void)
{
  int sz = RAM / 3 * 2;
  char *p = sbrk(sz);
  if (p == SBRK_ERROR)
    fail("sbrk");
  fill(p, sz, 3);

  int pid = fork();
  if (pid < 0)
    fail("fork");
  if (pid == 0) {
    for (int i = 0; i < sz; i += PG)
      p[i] = 9;
    exit(0);
  }
  waitstatus(); // el estado puede ser -1 (hijo matado por falta de memoria)
  if (!check(p, sz, 3))
    fail("el padre perdio sus datos tras agotar la memoria");
  sbrk(-sz);
}

static void
test_leak(void)
{
  int sz = RAM / 4;
  char *p = sbrk(sz);
  if (p == SBRK_ERROR)
    fail("sbrk");
  fill(p, sz, 5);

  for (int r = 0; r < 15; r++) {
    int pid = fork();
    if (pid < 0)
      fail("fork: fuga de memoria?");
    if (pid == 0) {
      fill(p, sz, 6); // copia todas las paginas
      exit(0);
    }
    if (waitstatus() != 0)
      fail("hijo fallo");
  }
  if (!check(p, sz, 5))
    fail("el padre perdio sus datos");
  sbrk(-sz);

  // si hubo fugas, ya no cabe una region grande.
  int big = RAM / 3 * 2;
  char *q = sbrk(big);
  if (q == SBRK_ERROR)
    fail("fuga de paginas: no se pudo reservar memoria al final");
  sbrk(-big);
}

#define RUN(name, call) \
  do {                  \
    cur = name;         \
    call;               \
    printf("test %s: ok\n", name); \
  } while (0)

int
main(void)
{
  RUN("simple", test_simple());
  RUN("three", test_many(3, RAM / 8));
  RUN("many", test_many(20, 256 * PG));
  RUN("isolation", test_isolation());
  RUN("copyout", test_copyout());
  RUN("refcount", test_refcount());
  RUN("oom", test_oom());
  RUN("leak", test_leak());
  printf("ALL COW TESTS PASSED\n");
  exit(0);
}
