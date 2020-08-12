#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
	int n;
	struct proc *curr_proc = myproc();
	if(curr_proc->strace == 1) {
		printf("[%d] sys_exit(%d)\n", curr_proc->pid, n);
	}
	int status = argint(0, &n);
	if(status < 0)
		return -1;
	exit(n);
	return 0; // not reached
}

uint64
sys_getpid(void)
{
	struct proc *curr_proc = myproc();
	if(curr_proc->strace == 1) {
		printf("[%d] sys_getpid()\n", curr_proc->pid);
	}
	return myproc()->pid;
}

uint64
sys_fork(void)
{
	struct proc *curr_proc = myproc();
	if(curr_proc->strace == 1) {
		printf("[%d] sys_fork()\n", curr_proc->pid);
	}
	return fork();
}

uint64
sys_wait(void)
{
	struct proc *curr_proc = myproc();
	if(curr_proc->strace == 1) {
		printf("[%d] sys_wait()\n", curr_proc->pid);
	}
	uint64 p;
	if(argaddr(0, &p) < 0)
		return -1;
	return wait(p);
}

uint64
sys_sbrk(void)
{
	int addr;
	int n;
	int status = argint(0, &n);
	struct proc *curr_proc = myproc();
	if(curr_proc->strace == 1) {
		printf("[%d] sys_sbrk(%d)\n", curr_proc->pid, n);
	}
	if(status < 0)
		return -1;
	addr = myproc()->sz;
	if(growproc(n) < 0)
		return -1;
	return addr;
}

uint64
sys_sleep(void)
{
	int n;
	uint ticks0;
	int status = argint(0, &n);
	struct proc *curr_proc = myproc();
	if(curr_proc->strace == 1) {
		printf("[%d] sys_sleep(%d)\n", curr_proc->pid, n);
	}
	if(status < 0)
		return -1;
	acquire(&tickslock);
	ticks0 = ticks;
	while(ticks - ticks0 < n) {
		if(myproc()->killed) {
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
	int status = argint(0, &pid);
	struct proc *curr_proc = myproc();
	if(curr_proc->strace == 1) {
		printf("[%d] sys_kill(%d)\n", curr_proc->pid, pid);
	}
	if(status < 0)
		return -1;
	return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
	uint xticks;
	struct proc *curr_proc = myproc();
	if(curr_proc->strace == 1) {
		printf("[%d] sys_uptime()\n", curr_proc->pid);
	}
	acquire(&tickslock);
	xticks = ticks;
	release(&tickslock);
	return xticks;
}

uint64
sys_psinfo(void)
{
	uint64 ptable_pt;
	uint64 count;
	argaddr(0, &ptable_pt);
	argaddr(1, &count);

	psinfo(ptable_pt, count);
	return 0;
}

uint64
sys_cinfo(void)
{
	uint64 ptable_pt;
	uint64 count;
	argaddr(0, &ptable_pt);
	argaddr(1, &count);

	return cinfo(ptable_pt, count);
}

uint64
sys_cinit(void)
{
	char name[16], path[MAXPATH];
	int max_proc, max_page, max_disk;
	argstr(0, name, 16);
	argstr(1, path, MAXPATH);
	argint(2, &max_proc);
	argint(3, &max_page);
	argint(4, &max_disk);

	return cinit(myproc(), name, path, max_proc, max_page, max_disk);
}

uint64
sys_cpause(void)
{
	char name[16];
	argstr(0, name, 16);

	return cpause(name);;
}

uint64
sys_cresume(void)
{
	char name[16];
	argstr(0, name, 16);

	return cresume(name);
}

uint64
sys_cstop(void)
{
	char name[16];
	argstr(0, name, 16);

	return cstop(name);
}
