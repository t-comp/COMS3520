overview:
this repo contains projects from COMS 3520 that involve developing
user level programs and kernel mods for xv6-riscv which is an
operating system that emulates unix and used for teaching purposes.

projects:

  project-1a/
    user level program in xv6. implements relay_plus.c,
    which is for selective worker routing for multiple procceses
    using a star/spoke architecture

  project-1b/
    kernel mods to xv6. adds new system calls for
    monitoring processes and managing them. those are
    getprocinfo, blockchild, unblockchild, and getresourceusage.

  project-1c/
    kernel mods to xv6. a priority scheduler with aging as an 
    alternative to round robin which is the default scheduler. 
    also adds new system calls

note:
each project folder contains only the files written or modified
for that project. the base xv6-riscv source code is not included.

you can find the full source code for that here: https://github.com/mit-pdos/xv6-riscv
