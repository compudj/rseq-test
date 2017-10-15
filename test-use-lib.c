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
#include "rseq.h"
#include "cpu-op.h"

static inline pid_t gettid(void)
{
	return syscall(__NR_gettid);
}

static int opt_threads = 200, opt_reps = 5000, opt_disable_mod = 0;

struct test_data_entry {
	uintptr_t count;
} __attribute__((aligned(128)));

struct inc_test_data {
	struct test_data_entry c[CPU_SETSIZE];
};

struct inc_thread_test_data {
	struct inc_test_data *data;
	int reps;
	int reg;
};

void *test_percpu_inc_thread(void *arg)
{
	struct inc_thread_test_data *thread_data = arg;
	struct inc_test_data *data = thread_data->data;
	int i;

	if (thread_data->reg && rseq_register_current_thread())
		abort();
	for (i = 0; i < thread_data->reps; i++) {
		int cpu;

#ifndef SKIP_FASTPATH
		struct rseq_state rseq_state;
		intptr_t *targetptr, newval;

		/* Try fast path. */
		rseq_state = rseq_start();
		cpu = rseq_cpu_at_start(rseq_state);
		newval = (intptr_t)data->c[cpu].count + 1;
		targetptr = (intptr_t *)&data->c[cpu].count;
		if (unlikely(!rseq_finish(targetptr, newval, rseq_state)))
#endif
		{
			for (;;) {
				/* Fallback on cpu_op system call. */
				int ret;

				cpu = rseq_current_cpu_raw();
				ret = cpu_op_add(&data->c[cpu].count, 1,
					sizeof(intptr_t), cpu);
				if (likely(!ret))
					break;
				assert(ret >= 0 || errno == EAGAIN);
			}
		}

#ifndef BENCHMARK
		if (i != 0 && !(i % (thread_data->reps / 10)))
			printf("tid %d: count %d\n", (int) gettid(), i);
#endif
	}
	if (rseq_unregister_current_thread())
		abort();
	return NULL;
}

void test_percpu_inc(void)
{
	const int num_threads = opt_threads;
	int i, ret;
	uintptr_t sum;
	pthread_t test_threads[num_threads];
	struct inc_test_data data;
	struct inc_thread_test_data thread_data[num_threads];

	memset(&data, 0, sizeof(data));
	for (i = 0; i < num_threads; i++) {
		thread_data[i].reps = opt_reps;
		if (opt_disable_mod <= 0 || (i % opt_disable_mod))
			thread_data[i].reg = 1;
		else
			thread_data[i].reg = 0;
		thread_data[i].data = &data;
		ret = pthread_create(&test_threads[i], NULL,
			test_percpu_inc_thread, &thread_data[i]);
		if (ret) {
			errno = ret;
			perror("pthread_create");
			abort();
		}
	}

	for (i = 0; i < num_threads; i++) {
		pthread_join(test_threads[i], NULL);
		if (ret) {
			errno = ret;
			perror("pthread_join");
			abort();
		}
	}

	sum = 0;
	for (i = 0; i < CPU_SETSIZE; i++)
		sum += data.c[i].count;

	assert(sum == (uintptr_t)opt_reps * num_threads);
}

int main(int argc, char **argv)
{
	printf("counter increment\n");
	test_percpu_inc();
	return 0;
}
