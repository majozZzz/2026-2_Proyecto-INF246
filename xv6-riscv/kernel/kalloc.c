// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

#define PGIDX(pa) (((uint64)(pa)) / PGSIZE)

struct {
  struct spinlock lock;
  int cnt[PHYSTOP / PGSIZE];
} pgref;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&pgref.lock, "pgref");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    // kfree decrementa el contador; se inicia en 1 para que llegue
    // a 0 y la pagina entre de verdad a la lista libre.
    pgref.cnt[PGIDX(p)] = 1;
    kfree(p);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&pgref.lock);
  if(pgref.cnt[PGIDX(pa)] < 1)
    panic("kfree: refcnt");
  pgref.cnt[PGIDX(pa)]--;
  if(pgref.cnt[PGIDX(pa)] > 0){
    // todavia hay otros procesos usando esta pagina
    release(&pgref.lock);
    return;
  }
  release(&pgref.lock);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    acquire(&pgref.lock);
    pgref.cnt[PGIDX(r)] = 1;     // un unico dueno al nacer
    release(&pgref.lock);
  }
  return (void*)r;
}

// Incrementa el contador de referencias de la pagina pa
// (una referencia mas, por ejemplo en uvmcopy durante fork).
void
krefinc(void *pa)
{
  acquire(&pgref.lock);
  if(pgref.cnt[PGIDX(pa)] < 1)
    panic("krefinc");
  pgref.cnt[PGIDX(pa)]++;
  release(&pgref.lock);
}

// Devuelve el contador de referencias actual de la pagina pa.
int
krefget(void *pa)
{
  int n;

  acquire(&pgref.lock);
  n = pgref.cnt[PGIDX(pa)];
  release(&pgref.lock);
  return n;
}

// Opcional (depuracion): cuenta las paginas libres.
// Util para detectar fugas llamandola desde procdump (Ctrl-P).
int
kcountfree(void)
{
  int n = 0;
  struct run *r;

  acquire(&kmem.lock);
  for(r = kmem.freelist; r; r = r->next)
    n++;
  release(&kmem.lock);
  return n;
}
