#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main (int argc, char *argv[]){
	struct ptable *ptable;
	struct proc_info *p;
	int count;

	ptable = (struct ptable*)malloc(sizeof(struct ptable));

	psinfo(ptable, &count);

	printf("\tPID\tVPID\tMEM\tNAME\tCONT\tPARENT\n");
	for (p = ptable->procs; p < &ptable->procs[count]; p++) {
		printf("\t%d\t%d\t%dK\t%s\t%s\t%s\n",
		       p->pid,
		       p->vpid,
		       p->mem / 1000,
		       p->name,
		       p->container,
		       p->parent
		       );
	}
	exit(0);
}
