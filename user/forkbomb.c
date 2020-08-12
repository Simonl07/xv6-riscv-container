/* forkbomb.c - fork processes until fork fails. */

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
	int i = 0;
	int id;

	printf("forkbomb: started\n");
	while(1) {
		id = fork();
		if (id < 0) {
			printf("forkbomb: fork() failed, exiting");
			exit(-1);
		}

		if (id == 0) {
			/* In child, just loop forever. Use sleep so that we don't consume CPU */
			while (1) {
				sleep(10);
			}
		}

		i += 1;
		printf("forkbomb: fork count = %d\n", i);
	}

	exit(0);
}
