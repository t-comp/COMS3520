#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

// ** MOD ** priority scheduler global variables
// flag to switch between rr and priority scheduler
int priorityFlag = 0;
// number of priority levels (set by startPriority)
int midPriority = 0;
// ticks before aging boost (set by startPriority)
int priorityN = 0;

// ** MOD ** doubly linked list node for priority queues
struct pq_node {
  struct proc *p;       // pointer to the process
  struct pq_node *next; // next node in queue
  struct pq_node *prev; // previous node in queue
};

// ** MOD ** one head and tail per priority level (up to PRIORITY_MAX_LEVEL queues)
struct pq_node *pq_head[PRIORITY_MAX_LEVEL];
struct pq_node *pq_tail[PRIORITY_MAX_LEVEL];

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// ** MOD ** add a process to the end of the queue at the given priority level
void
priority_enqueue(int level, struct proc *p)
{
  // allocate a new node using a kernel page
  struct pq_node *node = (struct pq_node *)kalloc();
  node->p = p;
  node->next = 0;
  node->prev = pq_tail[level];

  // attach to end of existing list
  if(pq_tail[level])
    pq_tail[level]->next = node;
  else
    pq_head[level] = node;  // list was empty, this is also the head

  pq_tail[level] = node;
  p->inQueue = 1;  // mark process as queued
}

// ** MOD ** remove and return the process at the front of the queue at given level
struct proc*
priority_dequeue(int level)
{
  // nothing to dequeue
  if(pq_head[level] == 0)
    return 0;

  struct pq_node *node = pq_head[level];
  struct proc *p = node->p;

  // advance head pointer
  pq_head[level] = node->next;
  if(pq_head[level])
    pq_head[level]->prev = 0;
  else
    pq_tail[level] = 0;  // queue is now empty

  kfree((void *)node);
  p->inQueue = 0;  // mark process as no longer queued
  return p;
}

// ** MOD ** remove a specific process from wherever it is in the queue at given level
void
priority_delete(int level, struct proc *p)
{
  struct pq_node *node = pq_head[level];

  // walk the list looking for the matching process
  while(node){
    if(node->p == p){
      // patch around the node being removed
      if(node->prev)
        node->prev->next = node->next;
      else
        pq_head[level] = node->next;  // removed head

      if(node->next)
        node->next->prev = node->prev;
      else
        pq_tail[level] = node->prev;  // removed tail

      kfree((void *)node);
      p->inQueue = 0;
      return;
    }
    node = node->next;
  }
}

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;

  // ** MOD ** reset fields
  // reset blocking state
  p->state_extra = UNBLOCKED;
  // reset cpu tick counter
  p->cpuTicks = 0;
  // reset syscall counter
  p->syscallCount = 0;
  // reset context switch counter
  p->contextSwitches = 0;
  // reset sleep counter
  p->sleepCount = 0;   

  // reset waiting ticks counter
  p->ticksWaiting = 0;

  // ** MOD ** reset fields added in project 1c
  p->priority = 0;
  // reset ticks spent on current queue
  p->ticksOnQueue = 0;
  // mark as not in any queue         
  p->inQueue = 0;              
  for(int i = 0; i < PRIORITY_MAX_LEVEL; i++)
    // clear per-level tick history
    p->tickCounts[i] = 0;      
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if(sz + n > TRAPFRAME) {
      return -1;
    }
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
kexit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}
// ** MOD ** round robin scheduler which iterates thru the proc table and runs each runnable p in order
void rrsched(struct cpu *c) {
  struct proc *p;
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    // skip processes that are blocked or not runnable
    if(p->state == RUNNABLE && p->state_extra == UNBLOCKED){
      p->state = RUNNING;
      c->proc = p;
      p->contextSwitches++;
      swtch(&c->context, &p->context);
      c->proc = 0;
    }
    release(&p->lock);
  }
}

// ** MOD ** priority scheduler which implements all 5 rules and picks the highest priority runnable process and runs it
void psched(struct cpu *c) {
  struct proc *p = 0;

  // r2: scan proc table for new runnable processes not yet in any queue
  for(struct proc *pp = proc; pp < &proc[NPROC]; pp++){
    acquire(&pp->lock);
    if(pp->state == RUNNABLE && pp->state_extra == UNBLOCKED && !pp->inQueue){
      pp->priority = midPriority / 2;
      priority_enqueue(pp->priority, pp);
    }
    release(&pp->lock);
  }

  // r5: aging which increases the wait ticks for the q'd processes, up the priority of a process if a certain threshold for waiting has been reached
  for(int i = 1; i < midPriority; i++){
    struct pq_node *node = pq_head[i];
    while(node){
      // save next before possible delete
      struct pq_node *next = node->next;
      node->p->ticksWaiting++;
      if(node->p->ticksWaiting >= priorityN){
        struct proc *bp = node->p;
        // remove from current queue before upping priority
        priority_delete(i, bp);
        bp->priority = i - 1;
        bp->ticksWaiting = 0;
        bp->ticksOnQueue = 0;
        // add to higher priority q
        priority_enqueue(i - 1, bp);
      }
      node = next;
    }
  }

  // r3: scan queues from highest priority to lowest
  for(int i = 0; i < midPriority; i++){
    struct pq_node *node = pq_head[i];
    while(node){
      if(node->p->state == RUNNABLE && node->p->state_extra == UNBLOCKED){
        p = node->p;
        priority_delete(i, p);
        break;
      }
      node = node->next;
    }
    if(p) break;
  }

  if(p == 0) {
    return;
  }

  // context switch to the p that was chosen
  acquire(&p->lock);
  p->state = RUNNING;
  c->proc = p;
  p->ticksWaiting = 0;
  p->contextSwitches++;
  swtch(&c->context, &p->context);
  c->proc = 0;
  release(&p->lock);

  // process back from running enqueue if still runnable and not blocked
  if(p->state == RUNNABLE || p->state == SLEEPING){
    // r3: time slice at level  2(level + 1)
    int ts = 2 * (p->priority + 1);
    p->ticksOnQueue++;
    p->tickCounts[p->priority]++;

    if(p->ticksOnQueue >= ts){
      // r4: time exp, make lower priotity if not at lowest
      if(p->priority < midPriority - 1)
        p->priority++;
      p->ticksOnQueue = 0;
    }

    // enqueu again if still runnable and not blocked
    if(p->state == RUNNABLE && p->state_extra == UNBLOCKED)
      priority_enqueue(p->priority, p);
  }
}

// ** MOD ** scheduler point that sends the control to the ps or rr
void scheduler(void) {
  struct cpu *c = mycpu();
  c->proc = 0;
  for(;;){
    intr_on();
    intr_off();
    // send to the active sched
    if(priorityFlag){
      psched(c);
    } else {
      rrsched(c);
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched RUNNING");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // Still holding p->lock from scheduler.
  release(&p->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();

    // We can invoke kexec() now that file system is initialized.
    // Put the return value (argc) of kexec into a0.
    p->trapframe->a0 = kexec("/init", (char *[]){ "/init", 0 });
    if (p->trapframe->a0 == -1) {
      panic("exec");
    }
  }

  // return to user space, mimicing usertrap()'s return.
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// Sleep on channel chan, releasing condition lock lk.
// Re-acquires lk when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  p->sleepCount++;  // ** MOD ** count voluntary cpu relinquishment (from 1b)

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on channel chan.
// Caller should hold the condition lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kkill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

// transitions the state_extra of the specified child process to blocked
int blockchild(int pid) {
  struct proc *p;
  struct proc *caller = myproc();

  // search proc table for target pid
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);

    // verify the target exists, is a direct child, and is not zombie or unused
    if(p->pid == pid && p->parent == caller &&
       p->state != ZOMBIE && p->state != UNUSED){

      // fail if already blocked
      if(p->state_extra == BLOCKED){
        release(&p->lock);
        return -1;
      }

      p->state_extra = BLOCKED;
      release(&p->lock);
      return 0;
    }

    release(&p->lock);
  }

  // pid not found or not a direct child
  return -1;
}

// transitions the state_extra of the specified child process back to unblocked
int unblockchild(int pid) {
  struct proc *p;
  struct proc *caller = myproc();

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);

    if(p->pid == pid && p->parent == caller &&
       p->state != ZOMBIE && p->state != UNUSED){

      // fail if not currently blocked
      if(p->state_extra == UNBLOCKED){
        release(&p->lock);
        return -1;
      }

      p->state_extra = UNBLOCKED;
      release(&p->lock);
      return 0;
    }

    release(&p->lock);
  }

  return -1;
}

// ** MOD ** function to activate ps where m = num priority levels and n = ticks before aging boost
// and then n = ticks before aging boost
int startPriority(int m, int n) {
  if(m <= 0 || m > PRIORITY_MAX_LEVEL || n <= 0)
    return -1;

  midPriority = m;
  priorityN = n;

  // clear all of the pq before switching
  for(int i = 0; i < PRIORITY_MAX_LEVEL; i++){
    pq_head[i] = 0;
    pq_tail[i] = 0;
  }

  // reset in queue flag for all p
  for(struct proc *p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    p->inQueue = 0;
    release(&p->lock);
  }

  // activate priority scheduler
  priorityFlag = 1;
  return 0;
}

// ** MOD ** deactivates priority scheduler and resumes rr
int
stopPriority(void)
{
  priorityFlag = 0;  // flip flag back to rr mode
  return 0;
}

// ** MOD ** functions that copies tick history to user space in each evel
int getPriorityInfo(struct PriorityInfoReport *rep) {
  struct proc *p = myproc();
  struct PriorityInfoReport r;

  // copy each level tick count
  for(int i = 0; i < PRIORITY_MAX_LEVEL; i++)
    r.tickCounts[i] = p->tickCounts[i];

  // copy report from kernel space to user space
  if(copyout(p->pagetable, (uint64)rep, (char *)&r, sizeof(r)) < 0)
    return -1;

  return 0;
}