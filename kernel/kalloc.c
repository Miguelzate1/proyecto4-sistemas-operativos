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

// NUEVO: Candado y arreglo para contar referencias físicas
struct spinlock ref_lock;
int ref_cnt[PHYSTOP / PGSIZE];

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&ref_lock, "ref_lock"); // NUEVO: Inicializar candado
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    // NUEVO: Evita que kfree falle al restar, pues asume que hay 1 referencia
    ref_cnt[(uint64)p / PGSIZE] = 1; 
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

  // NUEVO: Restar referencia de forma segura
  acquire(&ref_lock);
  int count = --ref_cnt[(uint64)pa / PGSIZE];
  release(&ref_lock);

  if(count > 0)
    return; // Alguien más la está usando, no la liberes todavía
  
  if(count < 0)
    panic("kfree: ref_cnt < 0"); // Control de errores

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

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    // NUEVO: Iniciar contador en 1 al entregar la página
    acquire(&ref_lock);
    ref_cnt[(uint64)r / PGSIZE] = 1;
    release(&ref_lock);
  }
  return (void*)r;
}

// NUEVO: Función para agregar referencias manualmente (se usará en fork)
void
krefinc(uint64 pa)
{
  if(pa % PGSIZE != 0 || (char*)pa < end || pa >= PHYSTOP)
    panic("krefinc");
  
  acquire(&ref_lock);
  if(ref_cnt[pa / PGSIZE] <= 0)
    panic("krefinc: intentando incrementar pagina libre");
  ref_cnt[pa / PGSIZE]++;
  release(&ref_lock);
}