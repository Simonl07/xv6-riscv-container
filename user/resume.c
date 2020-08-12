#include "kernel/fcntl.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
	char *fname;
	int rv, fd, pid;
	fname = argv[1];
	fd = open(fname, O_RDONLY);

	if (fd < 0) {
		printf("cannot open %s", fname);
		exit(-1);
	}

	pid = fork();
	if (pid != 0) {
		printf("resumed process %d from %s\n", pid, fname);
		exit(0);
	}

	if (pid == 0){
		rv = resume(fname);
		close(fd);

		if (rv < 0) {
			printf("resume(%s) failed\n", fname);
		}else{
			printf("resume(%s) success\n", fname);
		}
	}
	exit(0);
}
