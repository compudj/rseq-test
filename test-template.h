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

#include "rseq.h"
#include "cpu-op.h"

static int opt_reps = 5000;

struct test_data_entry {
	uintptr_t count;
} __attribute__((aligned(128)));

struct inc_test_data {
	struct test_data_entry c[CPU_SETSIZE];
};

static struct inc_test_data data;

static void TESTNAME(void)
{
	struct rseq_state rseq_state;
	intptr_t *targetptr, newval;
	uintptr_t sum;
	int i;

	printf("counter increment\n");
	for (i = 0; i < opt_reps; i++) {
		int cpu;

		/* Try fast path. */
		rseq_state = rseq_start();
		cpu = rseq_cpu_at_start(rseq_state);
		newval = (intptr_t)data.c[cpu].count + 1;
		targetptr = (intptr_t *)&data.c[cpu].count;
		if (unlikely(!rseq_finish(targetptr, newval, rseq_state))) {
			for (;;) {
				/* Fallback on cpu_op system call. */
				int ret;

				cpu = rseq_current_cpu_raw();
				ret = cpu_op_add(&data.c[cpu].count, 1,
					sizeof(intptr_t), cpu);
				if (likely(!ret))
					break;
				assert(ret >= 0 || errno == EAGAIN);
			}
		}
	}

	sum = 0;
	for (i = 0; i < CPU_SETSIZE; i++) {
		sum += data.c[i].count;
		data.c[i].count = 0;
	}

	printf("sum: %llu\n", (unsigned long long)sum);
	assert(sum == (uintptr_t)opt_reps);
}
