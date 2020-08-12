struct stat;
struct rtcdate;

struct proc_info {
	int pid;
	int vpid;
	int mem;
	char name[16];
	char parent[16];
	char container[16];
};

struct ptable {
	struct proc_info procs[64];
};

struct container_info {
	char name[16];
	char state[16];
	int current_container;
	int numproc;
	int maxproc;
	int memused;
	int memlimit;
	int diskused;
	int disklimit;
	char root[128];
	struct ptable ptable;
};

struct ctable {
	struct container_info containers[5];
};

// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int*);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);
int traceon(void);
int psinfo(struct ptable*, int*);
int suspend(int, int);
int resume(char *);
int cinfo(struct ctable*, int*);
int cinit(char *, char *, int, int, int);
int cpause(char *);
int cresume(char *);
int cstop(char *);




// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
void fprintf(int, const char*, ...);
void printf(const char*, ...);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
void* malloc(uint);
void free(void*);
int atoi(const char*);
