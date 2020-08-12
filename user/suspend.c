#include "kernel/fcntl.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
	int pid;
	char *fname;
	int rv;
	int fd;

	pid = atoi(argv[1]);
	fname = argv[2];

	fd = open(fname, O_CREATE | O_WRONLY);

	if (fd < 0) {
		printf("cannot open %d", fname);
		exit(-1);
	}

	rv = suspend(pid, fd);

	close(fd);

	if (rv < 0) {
		printf("suspend(%d, %s) failed\n", pid, fname);
		unlink(fname);
	}else{
		printf("suspend(%d, %s) success\n", pid, fname);
		kill(pid);
	}

	exit(0);
}
