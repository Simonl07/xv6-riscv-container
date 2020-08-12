#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "strings.h"
#include "resumable.h"


struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct container containers[NCONTS];

struct container *root = &containers[0];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void wakeup1(struct proc *chan);

extern char trampoline[]; // trampoline.S

int started = 0;

static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)
{
	uint i, n;
	uint64 pa;

	if((va % PGSIZE) != 0)
		panic("loadseg: va must be page aligned");

	for(i = 0; i < sz; i += PGSIZE) {
		pa = walkaddr(pagetable, va + i);
		if(pa == 0)
			panic("loadseg: address should exist");
		if(sz - i < PGSIZE)
			n = sz - i;
		else
			n = PGSIZE;
		if(readi(ip, 0, (uint64)pa, offset+i, n) != n)
			return -1;
	}

	return 0;
}

void
procinit(void)
{
	struct proc *p;
	struct container *c;
	initlock(&pid_lock, "nextpid");

	for(c = &containers[0]; c < &containers[NCONTS]; c++) {
		c->state = CUNUSED;
		c->nextvpid = 1;
		c->maxproc = 5;
		c->memused = 0;
		initlock(&c->vpid_lock, "vpid");
		initlock(&c->lock, "container");
	}

	// char *root_name = "root";
	// strncpy(root->name, root_name, 16);
	// root->state = CRUNNING;
	// root->max_proc = NPROC;

	for(p = proc; p < &proc[NPROC]; p++) {
		initlock(&p->lock, "proc");

		// Allocate a page for the process's kernel stack.
		// Map it high in memory, followed by an invalid
		// guard page.
		char *pa = kalloc();
		if(pa == 0)
			panic("kalloc");
		uint64 va = KSTACK((int) (p - proc));
		kvmmap(va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
		p->kstack = va;
	}

	kvminithart();
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
mycpu(void) {
	int id = cpuid();
	struct cpu *c = &cpus[id];
	return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void) {
	if (started == 0) {
		return (struct proc*)-1;
	}
	push_off();
	struct cpu *c = mycpu();
	struct proc *p = c->proc;
	pop_off();
	return p;
}

// Return the current struct proc *, or zero if none.
struct container*
mycont(void) {
	if (started == 0) {
		return (struct container*)-1;
	}
	struct proc *p = myproc();
	return p->container;
}

int
numproc(struct container *c){
	struct proc *p;
	int count = 0;
	for(p = proc; p < &proc[NPROC]; p++) {
		if (p->state != UNUSED && p->container == c) {
			count++;
		}
	}
	return count;
}

int
allocpid() {
	int pid;

	acquire(&pid_lock);
	pid = nextpid;
	nextpid = nextpid + 1;
	release(&pid_lock);

	return pid;
}

int allocvpid(struct container *c){
	int pid;

	acquire(&c->vpid_lock);
	pid = c->nextvpid;
	c->nextvpid++;
	release(&c->vpid_lock);
	return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, return 0.
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

	// Allocate a trapframe page.
	if((p->tf = (struct trapframe *)kalloc()) == 0) {
		release(&p->lock);
		return 0;
	}

	// An empty user page table.
	p->pagetable = proc_pagetable(p);

	// Set up new context to start executing at forkret,
	// which returns to user space.
	memset(&p->context, 0, sizeof p->context);
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
	if(p->tf)
		kfree((void*)p->tf);
	p->tf = 0;
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
}

// Create a page table for a given process,
// with no user pages, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
	pagetable_t pagetable;

	// An empty page table.
	pagetable = uvmcreate();

	// map the trampoline code (for system call return)
	// at the highest user virtual address.
	// only the supervisor uses it, on the way
	// to/from user space, so not PTE_U.
	mappages(pagetable, TRAMPOLINE, PGSIZE,
	         (uint64)trampoline, PTE_R | PTE_X);

	// map the trapframe just below TRAMPOLINE, for trampoline.S.
	mappages(pagetable, TRAPFRAME, PGSIZE,
	         (uint64)(p->tf), PTE_R | PTE_W);

	return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
	uvmunmap(pagetable, TRAMPOLINE, PGSIZE, 0);
	uvmunmap(pagetable, TRAPFRAME, PGSIZE, 0);
	if(sz > 0)
		uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
	0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x05, 0x02,
	0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x05, 0x02,
	0x9d, 0x48, 0x73, 0x00, 0x00, 0x00, 0x89, 0x48,
	0x73, 0x00, 0x00, 0x00, 0xef, 0xf0, 0xbf, 0xff,
	0x2f, 0x69, 0x6e, 0x69, 0x74, 0x00, 0x00, 0x01,
	0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
	struct proc *p;

	p = allocproc();
	initproc = p;

	// allocate one user page and copy init's instructions
	// and data into it.
	uvminit(p->pagetable, initcode, sizeof(initcode));
	p->sz = PGSIZE;

	// prepare for the very first "return" from kernel to user.
	p->tf->epc = 0;    // user program counter
	p->tf->sp = PGSIZE; // user stack pointer

	safestrcpy(p->name, "initcode", sizeof(p->name));

	p->state = RUNNABLE;

	p->vpid = allocvpid(root);

	release(&p->lock);

	cinit(p, "root", "/", NPROC, 256, 256);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
	uint sz;
	struct proc *p = myproc();

	sz = p->sz;
	if(n > 0) {
		if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
			return -1;
		}
	} else if(n < 0) {
		sz = uvmdealloc(p->pagetable, sz, sz + n);
	}
	p->sz = sz;
	return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
	int i, pid;
	struct proc *np;
	struct proc *p = myproc();


	// Allocate process.
	if((np = allocproc()) == 0) {
		return -1;
	}

	// Copy user memory from parent to child.
	if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
		freeproc(np);
		release(&np->lock);
		return -1;
	}
	np->sz = p->sz;

	np->parent = p;

	// copy saved user registers.
	*(np->tf) = *(p->tf);

	// Cause fork to return 0 in the child.
	np->tf->a0 = 0;

	// increment reference counts on open file descriptors.
	for(i = 0; i < NOFILE; i++)
		if(p->ofile[i])
			np->ofile[i] = filedup(p->ofile[i]);
	np->cwd = idup(p->cwd);

	safestrcpy(np->name, p->name, sizeof(p->name));

	pid = np->pid;

	np->state = RUNNABLE;

	struct container *c = p->container;

	// If exceed maxproc
	if(numproc(c) >= c->maxproc) {
		freeproc(np);
		release(&np->lock);
		printf("hit max proc for container %s\n", c->name);
		return -1;
	}

	np->container = c;
	np->vpid = allocvpid(c);

	release(&np->lock);

	return pid;
}

// Pass p's abandoned children to init.
// Caller must hold p->lock.
void
reparent(struct proc *p)
{
	struct proc *pp;

	for(pp = proc; pp < &proc[NPROC]; pp++) {
		// this code uses pp->parent without holding pp->lock.
		// acquiring the lock first could cause a deadlock
		// if pp or a child of pp were also in exit()
		// and about to try to lock p.
		if(pp->parent == p) {
			// pp->parent can't change between the check and the acquire()
			// because only the parent changes it, and we're the parent.
			acquire(&pp->lock);
			pp->parent = initproc;
			// we should wake up init here, but that would require
			// initproc->lock, which would be a deadlock, since we hold
			// the lock on one of init's children (pp). this is why
			// exit() always wakes init (before acquiring any locks).
			release(&pp->lock);
		}
	}
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
	struct proc *p = myproc();

	if(p == initproc)
		panic("init exiting");

	// Close all open files.
	for(int fd = 0; fd < NOFILE; fd++) {
		if(p->ofile[fd]) {
			struct file *f = p->ofile[fd];
			fileclose(f);
			p->ofile[fd] = 0;
		}
	}

	begin_op();
	iput(p->cwd);
	end_op();
	p->cwd = 0;

	// we might re-parent a child to init. we can't be precise about
	// waking up init, since we can't acquire its lock once we've
	// acquired any other proc lock. so wake up init whether that's
	// necessary or not. init may miss this wakeup, but that seems
	// harmless.
	acquire(&initproc->lock);
	wakeup1(initproc);
	release(&initproc->lock);

	// grab a copy of p->parent, to ensure that we unlock the same
	// parent we locked. in case our parent gives us away to init while
	// we're waiting for the parent lock. we may then race with an
	// exiting parent, but the result will be a harmless spurious wakeup
	// to a dead or wrong process; proc structs are never re-allocated
	// as anything else.
	acquire(&p->lock);
	struct proc *original_parent = p->parent;
	release(&p->lock);

	// we need the parent's lock in order to wake it up from wait().
	// the parent-then-child rule says we have to lock it first.
	acquire(&original_parent->lock);

	acquire(&p->lock);

	// Give any children to init.
	reparent(p);

	// Parent might be sleeping in wait().
	wakeup1(original_parent);

	p->xstate = status;
	p->state = ZOMBIE;

	release(&original_parent->lock);

	// Jump into the scheduler, never to return.
	sched();
	panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
	struct proc *np;
	int havekids, pid;
	struct proc *p = myproc();

	// hold p->lock for the whole time to avoid lost
	// wakeups from a child's exit().
	acquire(&p->lock);

	for(;;) {
		// Scan through table looking for exited children.
		havekids = 0;
		for(np = proc; np < &proc[NPROC]; np++) {
			// this code uses np->parent without holding np->lock.
			// acquiring the lock first would cause a deadlock,
			// since np might be an ancestor, and we already hold p->lock.
			if(np->parent == p) {
				// np->parent can't change between the check and the acquire()
				// because only the parent changes it, and we're the parent.
				acquire(&np->lock);
				havekids = 1;
				if(np->state == ZOMBIE) {
					// Found one.
					pid = np->pid;
					if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
					                        sizeof(np->xstate)) < 0) {
						release(&np->lock);
						release(&p->lock);
						return -1;
					}
					freeproc(np);
					release(&np->lock);
					release(&p->lock);
					return pid;
				}
				release(&np->lock);
			}
		}

		// No point waiting if we don't have any children.
		if(!havekids || p->killed) {
			release(&p->lock);
			return -1;
		}

		// Wait for a child to exit.
		sleep(p, &p->lock); //DOC: wait-sleep
	}
}
unsigned long randstate = 1;
unsigned int rand(int rand_max)
{
	randstate = randstate * 1664525 + 1013904223;
	int rand=((unsigned)(randstate/65536) % rand_max);
	return rand;
}


int
get_next_active_container(int index){
	for(;;) {
		index = (index + 1) % NCONTS;
		if (containers[index].state == CRUNNING) {
			return index;
		}

	}
	return 0;
}


// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
	struct proc *p;
	struct cpu *cpu = mycpu();
	int cindex = get_next_active_container(0);

	cpu->proc = 0;
	for(;;) {
		// Avoid deadlock by ensuring that devices can interrupt.
		intr_on();

		for(p = proc; p < &proc[NPROC]; p++) {
			acquire(&p->lock);
			if(p->state == RUNNABLE && p->container == &containers[cindex]) {
				// Switch to chosen process.  It is the process's job
				// to release its lock and then reacquire it
				// before jumping back to us.
				p->state = RUNNING;
				cpu->proc = p;
				swtch(&cpu->scheduler, &p->context);

				// Process is done running for now.
				// It should have changed its p->state before coming back.
				cpu->proc = 0;
				cindex = get_next_active_container(cindex);
				// printf("running %s next\n", containers[cindex].name);
			}
			release(&p->lock);
		}
		cindex = get_next_active_container(cindex);

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
		panic("sched running");
	if(intr_get())
		panic("sched interruptible");

	intena = mycpu()->intena;
	swtch(&p->context, &mycpu()->scheduler);
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
	static int first = 1;

	// Still holding p->lock from scheduler.
	release(&myproc()->lock);

	if (first) {
		// File system initialization must be run in the context of a
		// regular process (e.g., because it calls sleep), and thus cannot
		// be run from main().
		first = 0;
		fsinit(ROOTDEV);
	}

	usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
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
	if(lk != &p->lock) { //DOC: sleeplock0
		acquire(&p->lock); //DOC: sleeplock1
		release(lk);
	}

	// Go to sleep.
	p->chan = chan;
	p->state = SLEEPING;

	sched();

	// Tidy up.
	p->chan = 0;

	// Reacquire original lock.
	if(lk != &p->lock) {
		release(&p->lock);
		acquire(lk);
	}
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
	struct proc *p;

	for(p = proc; p < &proc[NPROC]; p++) {
		acquire(&p->lock);
		if(p->state == SLEEPING && p->chan == chan) {
			p->state = RUNNABLE;
		}
		release(&p->lock);
	}
}

// Wake up p if it is sleeping in wait(); used by exit().
// Caller must hold p->lock.
static void
wakeup1(struct proc *p)
{
	if(!holding(&p->lock))
		panic("wakeup1");
	if(p->chan == p && p->state == SLEEPING) {
		p->state = RUNNABLE;
	}
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
	struct proc *p;
	struct container *c = mycont();
	for(p = proc; p < &proc[NPROC]; p++) {

		int proc_pid;
		if (c == root) {
			proc_pid = p->pid;
		}else{
			proc_pid = p->vpid;
		}

		acquire(&p->lock);

		if(proc_pid == pid) {
			if(c != root && p->container != c) {
				release(&p->lock);
				return -1;
			}

			p->killed = 1;
			if(p->state == SLEEPING || p->state == SUSPENDED) {
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

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
	struct proc *p = myproc();
	if(user_dst) {
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
	if(user_src) {
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
		[SLEEPING]  "sleep ",
		[RUNNABLE]  "runble",
		[RUNNING]   "run   ",
		[ZOMBIE]    "zombie",
		[SUSPENDED] "suspended",
	};
	struct proc *p;
	char *state;

	printf("\n");
	for(p = proc; p < &proc[NPROC]; p++) {
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


struct ptable*
ptableof(struct container *c, int *sz){
	struct proc *p;
	int count = 0;
	struct ptable *ptable = (struct ptable*)kalloc();

	for(p = proc; p < &proc[NPROC]; p++) {
		acquire(&p->lock);
		if (p->state != UNUSED && (c == root || p->container == c)) {

			ptable->procs[count].pid = p->pid;
			ptable->procs[count].vpid = p->vpid;
			ptable->procs[count].mem = p->sz;
			strncpy(ptable->procs[count].name, p->name, 16);

			if (p->pid == 1) {
				strncpy(ptable->procs[count].parent, "", 16);
			}else{
				strncpy(ptable->procs[count].parent, p->parent->name, 16);

			}
			// release(&p->parent->lock);
			strncpy(ptable->procs[count].container, p->container->name, 16);
			count++;
		}
		release(&p->lock);
	}
	*sz = count;
	return ptable;
}

int
psinfo(uint64 ptable_pt, uint64 count_pt)
{
	int sz;
	struct ptable *ptable = ptableof(mycont(), &sz);
	copyout(myproc()->pagetable, count_pt, (void*)&sz, sizeof(sz));
	copyout(myproc()->pagetable, ptable_pt, (void*)ptable, sizeof(struct ptable));
	kfree(ptable);
	return 0;
}


int
suspend(int pid, struct file *f)
{
	int found = 0;
	struct proc *p;

	for (p = proc; p < &proc[NPROC]; p++) {
		acquire(&p->lock);
		if (p->pid == pid) {
			found = 1;
			p->state = SUSPENDED;
			release(&p->lock);

			struct resumehdr hdr;

			hdr.mem_sz = p->sz;
			hdr.code_sz = p->sz - 2 * PGSIZE;
			hdr.stack_sz = PGSIZE;
			hdr.strace = p->strace;
			strncpy(hdr.name, p->name, 16);
			pagetable_t tmp = myproc()->pagetable;
			filewritefromkernel(f, (uint64) &hdr, sizeof(struct resumehdr));
			filewritefromkernel(f, (uint64) p->tf, sizeof(struct trapframe));
			myproc()->pagetable = p->pagetable;
			filewrite(f, (uint64) 0, hdr.code_sz);
			filewrite(f, (uint64) (hdr.code_sz + PGSIZE), PGSIZE);
			myproc()->pagetable = tmp;
			break;
		}
		release(&p->lock);
	}

	if (!found) {
		return -1;
	}

	return 0;
}


int
resume(char *filename){
	// Alloc proc, read from file into proc struct return pid, release lock
	// int i, pid;
	// struct proc *p = myproc();
	struct resumehdr hdr;
	struct inode *ip;
	struct proc *p = myproc();
	pagetable_t pagetable = 0, oldpagetable = p->pagetable;
	uint64 oldsz = p->sz;
	uint64 sz = 0;
	uint64 stackbase, newstacksz, stackspace;


	begin_op(ROOTDEV);

	if ((ip = namei(filename)) == 0) {
		end_op(ROOTDEV);
		return -1;
	}

	// Lock inode
	ilock(ip);

	// Read header
	if (readi(ip, 0, (uint64) &hdr, 0, sizeof(struct resumehdr)) != sizeof(struct resumehdr)) {
		goto bad;
	}

	// Set offset as headersize
	int offset = sizeof(struct resumehdr);

	// Read trapframe into curr proc
	if (readi(ip, 0, (uint64) p->tf, offset, sizeof(struct trapframe)) < sizeof(struct trapframe)) {
		goto bad;
	}
	offset += sizeof(struct trapframe);

	// Empty user page table + alloc size
	if ((pagetable = proc_pagetable(p)) == 0 || (sz = uvmalloc(pagetable, sz, hdr.code_sz)) == 0) {
		goto bad;
	}

	// Read code
	if (loadseg(pagetable, 0, ip, offset, hdr.code_sz) < 0) {
		goto bad;
	}
	offset += hdr.code_sz;

	sz = PGROUNDUP(sz);
	stackbase = sz + PGSIZE;
	stackspace = sz;
	newstacksz = sz + 2*PGSIZE;

	// Alloc stack space
	if((sz = uvmalloc(pagetable, sz, newstacksz)) == 0) {
		goto bad;
	}

	// Clear stack space
	uvmclear(pagetable, stackspace);
	if (loadseg(pagetable, stackbase, ip, offset, hdr.stack_sz) < 0) {
		goto bad;
	}

	// Restore process name
	safestrcpy(p->name, hdr.name, strlen(hdr.name) + 1);

	// Restore proc struct and pagetable
	p->sz = hdr.mem_sz;
	p->strace = hdr.strace;
	p->pagetable = pagetable;

	// Free old proc table
	proc_freepagetable(oldpagetable, oldsz);

	// Release ilock
	iunlockput(ip);
	end_op(ROOTDEV);

	return 1;

bad:
	if(pagetable)
		proc_freepagetable(pagetable, sz);
	if(ip) {
		iunlockput(ip);
		end_op(ROOTDEV);
	}

	return -1;
}

int
cinit(struct proc *p, char *name, char *root_dir, int max_proc, int max_page, int max_disk)
{
	struct container *c;
	for (c = containers; c < &containers[NCONTS]; c++) {
		if (c->state == CUNUSED) {
			p->container = c;
			begin_op();
			if (p->container == root) {
				c->root = namexinit("/", 0, 0);
			}else{
				c->root = namei(root_dir);
			}
			end_op();
			strncpy(c->name, name, 16);
			p->cwd = c->root;
			c->state = CRUNNING;
			c->maxproc = max_proc;
			c->memlimit = max_page;
			c->memused = 0;
			c->disklimit = max_disk;
			c->diskused = 0;
			strncpy(c->root_dir, root_dir, strlen(root_dir));
			p->vpid = allocvpid(c);
			break;
		}
	}
	started = 1;
	return 0;
}


int
cinfo(uint64 ctable_pt, uint64 count_pt)
{
	if (mycont() != root) {
		return -1;
	}
	struct container *c;

	int count = 0;
	struct ctable *ctable = (struct ctable*)kalloc();

	for(c = containers; c < &containers[NCONTS]; c++) {
		acquire(&c->lock);
		if (c->state != CUNUSED) {
			strncpy(ctable->containers[count].name, c->name, 16);
			switch (c->state) {
			case CSUSPENDED:
				strncpy(ctable->containers[count].state, "SUSPENDED", 16); break;
			case CRUNNING:
				strncpy(ctable->containers[count].state, "RUNNING", 16); break;
			default:
				strncpy(ctable->containers[count].state, "UNKNOWN", 16); break;
			}
			int proc_sz;
			struct ptable *ptable = ptableof(c, &proc_sz);

			for(int i = 0; i < proc_sz; i++) {
				ctable->containers[count].ptable.procs[i] = ptable->procs[i];
			}
			kfree(ptable);
			ctable->containers[count].numproc = numproc(c);
			ctable->containers[count].maxproc = c->maxproc;
			// for(int i = 0; i < strlen(c->root_dir); i++) {
			// 	ctable->containers[count].root[i] = c->root_dir[i];
			// }
			strncpy(ctable->containers[count].root, c->root_dir, strlen(c->root_dir));
			ctable->containers[count].current_container = c == mycont();
			ctable->containers[count].memused = c->memused;
			ctable->containers[count].memlimit = c->memlimit;
			ctable->containers[count].diskused = c->diskused;
			ctable->containers[count].disklimit = c->disklimit;
			count++;
		}
		release(&c->lock);
	}
	copyout(myproc()->pagetable, count_pt, (void*)&count, sizeof(int));
	copyout(myproc()->pagetable, ctable_pt, (void*)ctable, sizeof(struct ctable));
	kfree(ctable);
	return 0;
}


int
cpause(char *name)
{
	if (mycont() != root) {
		return -1;
	}
	struct container *c;
	for(c = containers; c < &containers[NCONTS]; c++) {
		if (strncmp(c->name, name, strlen(name)) == 0) {
			c->state = CSUSPENDED;
			return 0;
		}
	}
	return -1;
}

int
cresume(char *name)
{
	if (mycont() != root) {
		return -1;
	}
	struct container *c;
	for(c = containers; c < &containers[NCONTS]; c++) {
		if (strncmp(c->name, name, strlen(name)) == 0) {
			c->state = CRUNNING;
			return 0;
		}
	}
	return -1;
}

int
cstop(char *name)
{
	if (mycont() != root) {
		return -1;
	}
	struct container *c;
	for(c = containers; c < &containers[NCONTS]; c++) {
		if (strncmp(c->name, name, strlen(name)) == 0) {
			c->state = CUNUSED;
			struct proc *p;
			for (p = proc; p < &proc[NPROC]; p++) {
				if (p->container == c) {
					freeproc(p);
				}
			}
			return 0;
		}
	}
	return -1;
}

int isroot(struct container *c){
	if (c == root) {
		return 1;
	}
	return 0;
}
