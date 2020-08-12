#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"


int
main(int argc, char *argv[])
{
	// int i;
	//
	// if(argc < 2){
	//   fprintf(2, "Usage: mkdir files...\n");
	//   exit(1);
	// }
	//
	// for(i = 1; i < argc; i++){
	//   if(mkdir(argv[i]) < 0){
	//     fprintf(2, "mkdir: %s failed to create\n", argv[i]);
	//     break;
	//   }
	// }
	//
	// exit(0);
	char *path = argv[1];

	if(mkdir(path) < 0) {
		printf("mkdir: %s failed to create\n", path);
		return -1;
	}
	for (int i = 2; i < argc; i++) {
		char *prog = argv[i];
		printf("%s\n", prog);

		int srcfd;
		int dstfd;
		if((srcfd = open(prog, O_RDONLY)) < 0) {
			printf("ccreate: cannot open %s\n", argv[i]);
			exit(1);
		}

		char filepath[100];
		strcpy(filepath, path);
		filepath[strlen(path)] = '/';
		strcpy(&filepath[strlen(path) + 1], prog);
		if((dstfd = open(filepath, O_CREATE | O_RDWR)) < 0) {
			printf("ccreate: cannot open %s\n", argv[i]);
			exit(1);
		}

		int n;
		char buf[1000];
		while((n = read(srcfd, buf, sizeof(buf))) > 0) {
			if (write(dstfd, buf, n) != n) {
				printf("cat: write error\n");
				exit(1);
			}
		}
		if(n < 0) {
			printf("cat: read error\n");
			exit(1);
		}
	}
	exit(0);
}
