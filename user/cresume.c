#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int
main(int argc, char *argv[])
{
	if (argc != 2) {
		printf("usage: cresume <name>\n");
		exit(-1);
	}
	char *name = argv[1];
	cresume(name);
	exit(0);
}
