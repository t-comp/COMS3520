Taylor Bauer
COMS 3520 - Project 1C

description:
	for this project a priority scheduler a priority scheduler has been
	implemented with round robin being the default in xv6 and then aging
	as an alternative. for this, the scheduler assigns different priorities
	to processes and then schedules the ones with higher priority first and
	then uses again to prevent starvation from happening. for more detail
	the priority scheduler uses the following rules:
		- rule 1: m priorities levels are defined where 0 = highest &
			  m - 1 = lowest. there are m number of ready queues
			  that are used, which is 1 for each level.
		- rule 2: when a process goes into the system, it's starting
			  priority is put at m/2 which is middle priority.
		- rule 3: the scheduler will always pick the process that is 
			  (1) the highest priority and (2) runnable. if there
			  are several with the same priority, they get 
			  scheduled using round robin. a p's time slice 
			  is 2(level they are at + 1)
		- rule 4: once a process used all of the time slice at a level,
			  it's priority is pushed down a level unless it's
	 		  already at the lowest level
		- rule 5: this uses aging, so basically after a process waits at
			  a specific level for say, n or more ticks of the time
			  slice then its priority is moved up a level unless it 
			  is already at the highest priority (level 0)
		
		
system calls (added):
(1) startPriority: activates the priority scheduler with m priority levels
    and n ticks before aging
(2) stopPriority: deactivates the priority scheduler and resumes round robin
(3) getPriorityInfo: returns the history of the time slice ticks for each level of the calling process

scheduler (added):
(1) rrsched: refactored original xv6 round-robin scheduler into its own function
(2) psched: priority scheduler that implements rule 1 - rule 5 listed above


added files:
- user/testsyscall.c: test program for priority scheduler syscalls
  added to makefile as testsyscall-2
- note: use testsyscall-2 <args> to run

modified files:
- kernel/proc.h: added PRIORITY_MAX_LEVEL, PriorityInfoReport struct,
  priority fields to struct proc (priority, ticksOnQueue, ticksWaiting,
  inQueue, tickCounts), function declarations for syscalls
- kernel/proc.c: added priority queue globals, pq_node struct, queue helper
  functions (priority_enqueue, priority_dequeue, priority_delete), implemented
  rrsched and psched, restructured scheduler to dispatch between rr and ps,
  implemented startPriority, stopPriority, getPriorityInfo
- kernel/syscall.h: added syscalls
- kernel/syscall.c: three new syscall handlers
- kernel/sysproc.c: implemented sys_startPriority, sys_stopPriority,
  sys_getPriorityInfo
- user/usys.pl: added entries for three new syscalls
- user/user.h: added PriorityInfoReport struct and declarations for
  three new syscalls


known limitations:
- output from the children seems to be interleaved with shell prompt idk