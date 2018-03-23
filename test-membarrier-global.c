#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <unistd.h>
#include <poll.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <urcu/arch.h>
#include <urcu/uatomic.h>
#include <urcu/compiler.h>

#include <linux/membarrier.h>
#include <unistd.h>

#ifdef __NR_membarrier
# define membarrier(...)		syscall(__NR_membarrier, __VA_ARGS__)
#else
# define membarrier(...)		-ENOSYS
#endif

#define MAP_LEN				4096

#define NR_LOOPS			100000000

enum mode {
	MODE_MB,
	MODE_MEMBARRIER,
	MODE_COMPILER_BARRIER,
	MODE_MEMBARRIER_MISSING_REGISTER,
};

enum mode mode;

struct map_test {
	int x, y;
	int ref;
	int r2, r4;
	int r2_ready, r4_ready;
	int killed;
	pthread_mutex_t lock;
};

static void check_parent_regs(struct map_test *map_test, int r2)
{
	pthread_mutex_lock(&map_test->lock);
	if (map_test->r4_ready) {
		if (r2 == 0 && map_test->r4 == 0) {
			fprintf(stderr, "Error detected!\n");
			CMM_STORE_SHARED(map_test->killed, 1);
			abort();
		}
		map_test->r4_ready = 0;
		map_test->x = 0;
		map_test->y = 0;
	} else {
		map_test->r2 = r2;
		map_test->r2_ready = 1;
	}
	pthread_mutex_unlock(&map_test->lock);
}

static void check_child_regs(struct map_test *map_test, int r4)
{
	pthread_mutex_lock(&map_test->lock);
	if (map_test->r2_ready) {
		if (r4 == 0 && map_test->r2 == 0) {
			fprintf(stderr, "Error detected!\n");
			CMM_STORE_SHARED(map_test->killed, 1);
			abort();
		}
		map_test->r2_ready = 0;
		map_test->x = 0;
		map_test->y = 0;
	} else {
		map_test->r4 = r4;
		map_test->r4_ready = 1;
	}
	pthread_mutex_unlock(&map_test->lock);
}

static void loop_parent(struct map_test *map_test)
{
	int i, r2;

	for (i = 0; i < NR_LOOPS; i++) {
		uatomic_inc(&map_test->ref);
		while (uatomic_read(&map_test->ref) < 2 * (i + 1)) {
			if (map_test->killed)
				abort();
			caa_cpu_relax();
		}
		CMM_STORE_SHARED(map_test->x, 1);
		switch (mode) {
		case MODE_MB:
			cmm_smp_mb();
			break;
		case MODE_MEMBARRIER:
		case MODE_MEMBARRIER_MISSING_REGISTER:
			if (membarrier(MEMBARRIER_CMD_GLOBAL_EXPEDITED, 0)) {
				perror("membarrier");
				CMM_STORE_SHARED(map_test->killed, 1);
				abort();
			}
			break;
		case MODE_COMPILER_BARRIER:
			cmm_barrier();
			break;
		}
		r2 = CMM_LOAD_SHARED(map_test->y);
		check_parent_regs(map_test, r2);
	}
}

static void loop_child(struct map_test *map_test)
{
	int i, r4;

	switch (mode) {
	case MODE_MEMBARRIER:
		if (membarrier(MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED, 0)) {
			perror("membarrier");
			CMM_STORE_SHARED(map_test->killed, 1);
			abort();
		}
		break;
	default:
		break;
	}
	for (i = 0; i < NR_LOOPS; i++) {
		uatomic_inc(&map_test->ref);
		while (uatomic_read(&map_test->ref) < 2 * (i + 1)) {
			if (map_test->killed)
				abort();
			caa_cpu_relax();
		}

		CMM_STORE_SHARED(map_test->y, 1);
		switch (mode) {
		case MODE_MB:
			cmm_smp_mb();
			break;
		case MODE_MEMBARRIER:
		case MODE_MEMBARRIER_MISSING_REGISTER:
			/* Child receives membarrier IPI. */
			cmm_barrier();
			break;
		case MODE_COMPILER_BARRIER:
			cmm_barrier();
			break;
		}
		r4 = CMM_LOAD_SHARED(map_test->x);
		check_child_regs(map_test, r4);
	}
}

void print_arg_error(void)
{
	fprintf(stderr, "Please specify test mode: <m>: paired mb, <s>: sys-membarrier, <c>: compiler barrier (error), <n>: sys-membarrier with missing registration (error).\n");
}

int main(int argc, char **argv)
{
	char namebuf[PATH_MAX];
	pid_t pid;
	int fd, ret = 0;
	void *buf;
	struct map_test *map_test;
	pthread_mutexattr_t attr;

	if (argc < 2) {
		print_arg_error();
		return -1;
	}
	if (!strcmp(argv[1], "-m")) {
		mode = MODE_MB;
	} else if (!strcmp(argv[1], "-s")) {
		mode = MODE_MEMBARRIER;
	} else if (!strcmp(argv[1], "-c")) {
		mode = MODE_COMPILER_BARRIER;
	} else if (!strcmp(argv[1], "-n")) {
		mode = MODE_MEMBARRIER_MISSING_REGISTER;
	} else {
		print_arg_error();
		return -1;
	}

	buf = mmap(NULL, MAP_LEN, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (buf == MAP_FAILED) {
		perror("mmap");
		ret = -1;
		goto end;
	}
	map_test = (struct map_test *)buf;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_setpshared(&attr, 1);
	pthread_mutex_init(&map_test->lock, &attr);
	pid = fork();
	if (pid < 0) {
		perror("fork");
		ret = -1;
		goto unmap;
	}
	if (!pid) {
		/* child */
		loop_child(map_test);
		return 0;
	}
	/* parent */
	loop_parent(map_test);
	pid = waitpid(pid, NULL, 0);
	if (pid < 0) {
		perror("waitpid");
		ret = -1;
	}
unmap:
	pthread_mutex_destroy(&map_test->lock);
	pthread_mutexattr_destroy(&attr);
	if (munmap(buf, MAP_LEN)) {
		perror("munmap");
		ret = -1;
	}
end:
	return ret;
}
