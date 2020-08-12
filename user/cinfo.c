#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main (int argc, char *argv[]){
	struct ctable *ctable;
	struct container_info *c;
	struct proc_info *p;
	int count;

	ctable = (struct ctable*)malloc(sizeof(struct ctable));

	int rv = cinfo(ctable, &count);
	if (rv != 0) {
		printf("cinfo failed: cannot execute outside of root\n");
		free((void *)ctable);
		exit(-1);
	}


	for (c = ctable->containers; c < &ctable->containers[count]; c++) {
		char *curr = "";
		if (c->current_container == 1) {
			curr = "*";
		}

		printf("%sNAME:%s\tSTATE:%s\t#PROCESS:%d/%d\tROOT:%s\tMEM:%d/%d blocks\tDISK:%d/%d blocks\n",
		       curr,
		       c->name,
		       c->state,
		       c->numproc,
		       c->maxproc,
		       c->root,
		       c->memused,
		       c->memlimit,
		       c->diskused,
		       c->disklimit
		       );
		printf("\tPID\tVPID\tMEM\tNAME\tCONT\tPARENT\n");
		for (p = c->ptable.procs; p < &c->ptable.procs[c->numproc]; p++) {
			printf("\t%d\t%d\t%dK\t%s\t%s\t%s\n",
			       p->pid,
			       p->vpid,
			       p->mem / 1000,
			       p->name,
			       p->container,
			       p->parent
			       );
		}
		printf("\n");
	}
	free((void *)ctable);
	exit(0);
}
