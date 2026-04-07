#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if(t == SBRK_EAGER || n < 0) {
    if(growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if(addr + n < addr)
      return -1;
    if(addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64 sys_getprocinfo(void) {
  struct proc *p;
  struct proc_info info;
  uint64 uaddr;
  
  /*
  * myproc() returns a pointer to the struct proc
  * corresponding to the currently running process.
  * It is the standard way in xv6 to access the caller's
  * process control block.
  */
  p = myproc();

  /* Copy the relevant fields from the process control block
  * (struct proc *p) into the local struct proc_info 'info'.
  */
  info.pid = p->pid;
  info.ppid = p->parent ? p->parent->pid : 0;
  info.state = p->state;
  info.sz = p->sz;

  /*
  * argaddr() retrieves the user-space address of the first
  * argument passed to the system call. In this case, it is
  * the pointer where the proc_info structure should be copied.
  */
  argaddr(0, &uaddr);

  /*
  * copyout() safely copies data from kernel space to user space.
  * It takes the destination page table, user address, kernel
  * source address, and number of bytes to copy.
  *
  * The function returns a negative value on failure.
  */
  if(copyout(p->pagetable, uaddr, (char *)&info, sizeof(info)) < 0)
    return -1;

  return 0;
}

// ** MODS ** activates priority scheduler with m levels and n aging threshold
uint64 sys_startPriority(void) {
  int m, n;
  // num of priority levels
  argint(0, &m);
  // aging threshold
  argint(1, &n);
  return startPriority(m, n);
}

// deactivates ps and resumes rr
uint64 sys_stopPriority(void) {
  return stopPriority();
}

// copies tick history to users space for each level
uint64 sys_getPriorityInfo(void) {
  uint64 uad;
  // user space pointer
  argaddr(0, &uad);
  return getPriorityInfo((struct PriorityInfoReport *)uad);
}
