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

#include <rseq.h>

intptr_t v;

int main(int argc, char **argv)
{
	int cpu, ret;

	if (rseq_register_current_thread())
		abort();
	cpu = rseq_cpu_start();
	ret = rseq_addv(&v, 1, cpu);
	if (ret)
		goto end;
end:
	if (rseq_unregister_current_thread())
		abort();
	printf("total %" PRIdPTR "\n", v);
	return 0;
}
