Taylor Bauer
COMS 3520 - Project 1A

description:
this project implements relay_plus.c, a multi-process command relay
program for xv6. it uses a star/spoke architecture where the parent communicates directly with each worker instead of a linear pipeline. this allows selective
routing of commands to specific workers.

features:
- :all - send command through all workers
- :first k - send command through first k workers only
- :skip k - send command through all workers except worker k
- :exit or :EXIT - gracefully terminate all workers

usage:
  relay_plus <num_workers>
  example: relay_plus 3

added files:
- user/relay_plus.c: main program
- Makefile: updated to include relay_plus