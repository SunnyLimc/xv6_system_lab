#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

pagetable_t acquire_globalkpgt() { return kernel_pagetable; }

/*
 * create a direct-map page table for the kernel.
 */
void
kvminit()
{
  kernel_pagetable = (pagetable_t)kalloc();
  kpginit(0);
}

// if pg == 0, stands for global kernel pagetable
int kpginit(pagetable_t pg) {
  // use pgt for condition judging
  pagetable_t pgt = pg;
  if (pg == 0) pg = kernel_pagetable;
  const uint64 va[] = {
      // uart registers
      UART0,
      // virtio mmio disk interface
      VIRTIO0,
      // CLNT
      CLINT,
      // PLIC
      PLIC,
      // map kernel text executable and read-only.
      KERNBASE,
      // map kernel data and the physical RAM we'll make use of.
      (uint64)etext,
      // map the trampoline for trap entry/exit to
      // the highest virtual address in the kernel.
      TRAMPOLINE};
  const int count = sizeof(va) / sizeof(uint64);
  const uint64 sz[] = {PGSIZE,
                       PGSIZE,
                       0x10000,
                       0x400000,
                       (uint64)etext - KERNBASE,
                       PHYSTOP - (uint64)etext,
                       PGSIZE};
  // mapping
  memset(pg, 0, PGSIZE);
  const uint64 pa[] = {UART0,         VIRTIO0,           CLINT, PLIC, KERNBASE,
                       (uint64)etext, (uint64)trampoline};
  const uint64 perm[] = {PTE_R | PTE_W, PTE_R | PTE_W, PTE_R | PTE_W,
                         PTE_R | PTE_W, PTE_R | PTE_X, PTE_R | PTE_W,
                         PTE_R | PTE_X};
  for (int i = 0; i < count; i++) {
    //! do not map CLINT for per-process_kernel_pagetable to avoid interfering
    //! the lab
    // only map it for global_kernel_pagetable
    if (pgt != 0 && va[i] == CLINT) continue;
    if (kpgmap(pgt, va[i], pa[i], sz[i], perm[i]) != 0) return -1;
  }
  return 0;
}

int kpgmap(pagetable_t pg, uint64 va, uint64 pa, uint64 sz, uint64 perm) {
  if (pg == 0) {
    kvmmap(va, pa, sz, perm);
  } else {
    return mappages(pg, va, sz, pa, perm);
  }
  // everything is ok
  return 0;
}

void kvmhart(pagetable_t pg) {
  w_satp(MAKE_SATP(pg));
  sfence_vma();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kernel_pagetable, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// translate a kernel virtual address to
// a physical address. only needed for
// addresses on the stack.
// assumes va is page aligned.
uint64
kvmpa(uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;
  
  pte = walk(kernel_pagetable, va, 0);
  if(pte == 0)
    panic("kvmpa");
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa+off;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for (;;) {
    if ((pte = walk(pagetable, a, 1)) == 0) return -1;
    if (*pte & PTE_V) panic("remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    // map -> judge -> break
    // PGROUNDDOWN is feasible
    if (a == last) break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

int union_mappage(pagetable_t user_pg, pagetable_t kern_pg, uint64 va,
                  uint64 size, uint64 pa, int perm) {
  // avoid "remap" panic, deny mapping VA higher than PLIC (0x0C000000)
  if (va < PLIC) {
    if (va + size - 1 >= PLIC) size = PLIC - va;
    if (mappages(user_pg, va, size, pa, perm) != 0) return -1;
    //! watch out bugs from permission settings
    if (mappages(kern_pg, va, size, pa, perm & ~PTE_U) != 0) {
      uvmunmap(user_pg, va, size / PGSIZE, 0);
      return -1;
    }
  }
  return 0;
}

void union_unmappage(pagetable_t user_pg, pagetable_t kern_pg, uint64 va,
                     uint64 npages, int do_free) {
  if (va < PLIC) {
    if (va + npages * PGSIZE - 1 >= PLIC) npages = (PLIC - va) / PGSIZE;
    uvmunmap(user_pg, va, npages, do_free);
    uvmunmap(kern_pg, va, npages, 0);
  };
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free) {
  uint64 a;
  pte_t *pte;
  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if ((*pte & PTE_V) == 0) {
      printf("%p %p", pagetable, va);
      panic("uvmunmap: not mapped");
    }
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void uvminit(pagetable_t user_pg, pagetable_t kern_pg, uchar *src, uint sz) {
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  uint64 perm = PTE_W | PTE_R | PTE_X | PTE_U;
  mappages(user_pg, 0, PGSIZE, (uint64)mem, perm);
  mappages(kern_pg, 0, PGSIZE, (uint64)mem, perm & ~PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64 uvmalloc(pagetable_t user_pg, pagetable_t kern_pg, uint64 oldsz,
                uint64 newsz) {
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(user_pg, kern_pg, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if (union_mappage(user_pg, kern_pg, a, PGSIZE, (uint64)mem,
                      PTE_W | PTE_X | PTE_R | PTE_U) != 0) {
      kfree(mem);
      uvmdealloc(user_pg, kern_pg, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64 uvmdealloc(pagetable_t user_pg, pagetable_t kern_pg, uint64 oldsz,
                  uint64 newsz) {
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    union_unmappage(user_pg, kern_pg, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

void _unmapfreewalk(pagetable_t pg, int level) {
  for (int i = 0; i < 512; i++) {
    pte_t pte = pg[i];
    if (pte & PTE_V) {
      if (level > 0) {
        pagetable_t pa = (pagetable_t)PTE2PA(pte);
        _unmapfreewalk(pa, level - 1);
        kfree((void *)pa);
      }
      pg[i] = 0;
    }
  }
}

// unmap valid pages (but without freeing it) from last level
// and do freewalk for first two level
void unmapfreewalk(pagetable_t pg) {
  _unmapfreewalk(pg, 2);
  kfree((void *)pg);
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int uvmcopy(pagetable_t old, pagetable_t new_user, pagetable_t new_kern,
            uint64 sz) {
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if (union_mappage(new_user, new_kern, i, PGSIZE, (uint64)mem, flags) != 0) {
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
   union_unmappage(new_user, new_kern, 0, i / PGSIZE, 1);
   return -1;
 }

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  return copyin_new(pagetable, dst, srcva, len);
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  return copyinstr_new(pagetable, dst, srcva, max);
}

void _vmprint(pagetable_t ptepm, int level) {
  for (int i = 0; i < 512; i++) {
    pte_t pte = ptepm[i];
    if (pte & PTE_V) {
      pagetable_t pa = (pagetable_t)PTE2PA(pte);
      for (int i = 2; i > level; i--) {
        printf(".. ");
      }
      printf("..%d: ", i);
      printf("pte: %p pa: %p\n", pte, pa);
      if (level > 0) {
        _vmprint(pa, level - 1);
      }
    }
  }
}

int vmprint(pagetable_t pagetable) {
  printf("page table %p\n", pagetable);
  _vmprint(pagetable, 2);
  return 0;
}
