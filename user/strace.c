#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
	int id;
	char *progname = argv[1];

	id = fork();
	if (id == 0) {
		/* we are in the child */
		traceon();
		exec(progname, &argv[1]);
		exit(0);
	} else {
		/* we are in the parent */
		id = wait(&id);
	}
	exit(0);
}
