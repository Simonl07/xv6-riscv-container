#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int
main(int argc, char *argv[])
{
	int fd, id;

	if (argc != 7) {
		printf("usage: cstart <root_dir> <vc> <max_proc> <max_page> <max_disk> <cmd> [<arg> ...]\n");
		exit(-1);
	}
	char *root = argv[1];

	fd = open(argv[2], O_RDWR);
	printf("fd = %d\n", fd);

	int max_proc = atoi(argv[3]);

	int max_page = atoi(argv[4]);

	int max_disk = atoi(argv[5]);


	/* fork a child and exec argv[1] */
	id = fork();

	if (id == 0) {
		close(0);
		close(1);
		close(2);
		dup(fd);
		dup(fd);
		dup(fd);
		cinit(root, root, max_proc, max_page, max_disk);
		exec(argv[6], &argv[6]);
		exit(0);
	}

	printf("%s started on %s\n", argv[2], argv[3]);

	exit(0);
}
