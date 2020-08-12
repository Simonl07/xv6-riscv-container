#ifndef RESUMABLE_H
#define RESUMABLE_H

struct resumehdr {
	int mem_sz;
	int code_sz;
	int stack_sz;
	int strace;
	char name[16];
};

#endif
