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

#include <urcu/uatomic.h>

uintptr_t test_global_count;
volatile uintptr_t test_global_count_volatile;

static inline pid_t gettid(void)
{
	return syscall(__NR_gettid);
}

#define NR_INJECT	9
static int loop_cnt[NR_INJECT + 1];

static int opt_modulo;

static int opt_yield, opt_signal, opt_sleep, opt_fallback_cnt = 3,
		opt_disable_rseq, opt_threads = 200,
		opt_disable_mod = 0, opt_test = 's';

static long long opt_reps = 5000;

static __thread unsigned int signals_delivered;

#ifndef BENCHMARK

static __thread unsigned int yield_mod_cnt, nr_abort;

#define printf_nobench(fmt, ...)	printf(fmt, ## __VA_ARGS__)

#define RSEQ_INJECT_INPUT \
	, [loop_cnt_1]"m"(loop_cnt[1]) \
	, [loop_cnt_2]"m"(loop_cnt[2]) \
	, [loop_cnt_3]"m"(loop_cnt[3]) \
	, [loop_cnt_4]"m"(loop_cnt[4])

#if defined(__x86_64__) || defined(__i386__)

#define INJECT_ASM_REG	"eax"

#define RSEQ_INJECT_CLOBBER \
	, INJECT_ASM_REG

#define RSEQ_INJECT_ASM(n) \
	"mov %[loop_cnt_" #n "], %%" INJECT_ASM_REG "\n\t" \
	"test %%" INJECT_ASM_REG ",%%" INJECT_ASM_REG "\n\t" \
	"jz 333f\n\t" \
	"222:\n\t" \
	"dec %%" INJECT_ASM_REG "\n\t" \
	"jnz 222b\n\t" \
	"333:\n\t"

#elif defined(__ARMEL__)

#define INJECT_ASM_REG	"r4"

#define RSEQ_INJECT_CLOBBER \
	, INJECT_ASM_REG

#define RSEQ_INJECT_ASM(n) \
	"ldr " INJECT_ASM_REG ", %[loop_cnt_" #n "]\n\t" \
	"cmp " INJECT_ASM_REG ", #0\n\t" \
	"beq 333f\n\t" \
	"222:\n\t" \
	"subs " INJECT_ASM_REG ", #1\n\t" \
	"bne 222b\n\t" \
	"333:\n\t"

#else
#error unsupported target
#endif

#define RSEQ_INJECT_FAILED \
	nr_abort++;

#define RSEQ_INJECT_C(n) \
{ \
	int loc_i, loc_nr_loops = loop_cnt[n]; \
	\
	for (loc_i = 0; loc_i < loc_nr_loops; loc_i++) { \
		barrier(); \
	} \
	if (loc_nr_loops == -1 && opt_modulo) { \
		if (yield_mod_cnt == opt_modulo - 1) { \
			if (opt_sleep > 0) \
				poll(NULL, 0, opt_sleep); \
			if (opt_yield) \
				sched_yield(); \
			if (opt_signal) \
				raise(SIGUSR1); \
			yield_mod_cnt = 0; \
		} else { \
			yield_mod_cnt++; \
		} \
	} \
}

#define RSEQ_FALLBACK_CNT	\
	opt_fallback_cnt

#else

#define printf_nobench(fmt, ...)

#endif /* BENCHMARK */

#include "rseq.h"
#include "cpu-op.h"

struct percpu_lock_entry {
	intptr_t v;
} __attribute__((aligned(128)));

struct percpu_lock {
	struct percpu_lock_entry c[CPU_SETSIZE];
};

struct test_data_entry {
	uintptr_t count;
} __attribute__((aligned(128)));

struct spinlock_test_data {
	struct percpu_lock lock;
	struct test_data_entry c[CPU_SETSIZE];
};

struct spinlock_thread_test_data {
	struct spinlock_test_data *data;
	long long reps;
	int reg;
};

struct inc_test_data {
	struct test_data_entry c[CPU_SETSIZE];
};

struct inc_thread_test_data {
	struct inc_test_data *data;
	long long reps;
	int reg;
};

struct percpu_list_node {
	intptr_t data;
	struct percpu_list_node *next;
};

struct percpu_list_entry {
	struct percpu_list_node *head;
} __attribute__((aligned(128)));

struct percpu_list {
	struct percpu_list_entry c[CPU_SETSIZE];
};

/* A simple percpu spinlock.  Returns the cpu lock was acquired on. */
static int rseq_percpu_lock(struct percpu_lock *lock)
{
	int cpu;

	for (;;) {
		int ret;

#ifndef SKIP_FASTPATH
		/* Try fast path. */
		cpu = rseq_cpu_start();
		ret = rseq_cmpeqv_storev(&lock->c[cpu].v,
				0, 1, cpu);
		if (rseq_likely(!ret))
			break;
		if (ret > 0)
			continue;	/* Retry. */
#endif
	slowpath:
		__attribute__((unused));
		/* Fallback on cpu_opv system call. */
		cpu = rseq_current_cpu();
		ret = cpu_op_cmpeqv_storev(&lock->c[cpu].v, 0, 1, cpu);
		if (rseq_likely(!ret))
			break;
		assert(ret >= 0 || errno == EAGAIN);
	}
	/*
	 * Acquire semantic when taking lock after control dependency.
	 * Matches rseq_smp_store_release().
	 */
	rseq_smp_acquire__after_ctrl_dep();
	return cpu;
}

static void rseq_percpu_unlock(struct percpu_lock *lock, int cpu)
{
	assert(lock->c[cpu].v == 1);
	/*
	 * Release lock, with release semantic. Matches
	 * rseq_smp_acquire__after_ctrl_dep().
	 */
	rseq_smp_store_release(&lock->c[cpu].v, 0);
}

void *test_percpu_spinlock_thread(void *arg)
{
	struct spinlock_thread_test_data *thread_data = arg;
	struct spinlock_test_data *data = thread_data->data;
	int cpu;
	long long i, reps;

	if (!opt_disable_rseq && thread_data->reg
			&& rseq_register_current_thread())
		abort();
	reps = thread_data->reps;
	for (i = 0; i < reps; i++) {
		cpu = rseq_percpu_lock(&data->lock);
		data->c[cpu].count++;
		rseq_percpu_unlock(&data->lock, cpu);
#ifndef BENCHMARK
		if (i != 0 && !(i % (reps / 10)))
			printf("tid %d: count %lld\n", (int) gettid(), i);
#endif
	}
	printf_nobench("tid %d: number of rseq abort: %d, signals delivered: %u\n",
		(int) gettid(), nr_abort, signals_delivered);
	if (rseq_unregister_current_thread())
		abort();
	return NULL;
}

/*
 * A simple test which implements a sharded counter using a per-cpu
 * lock.  Obviously real applications might prefer to simply use a
 * per-cpu increment; however, this is reasonable for a test and the
 * lock can be extended to synchronize more complicated operations.
 */
void test_percpu_spinlock(void)
{
	const int num_threads = opt_threads;
	int i, ret;
	uintptr_t sum;
	pthread_t test_threads[num_threads];
	struct spinlock_test_data data;
	struct spinlock_thread_test_data thread_data[num_threads];

	memset(&data, 0, sizeof(data));
	for (i = 0; i < num_threads; i++) {
		thread_data[i].reps = opt_reps;
		if (opt_disable_mod <= 0 || (i % opt_disable_mod))
			thread_data[i].reg = 1;
		else
			thread_data[i].reg = 0;
		thread_data[i].data = &data;
		ret = pthread_create(&test_threads[i], NULL,
			test_percpu_spinlock_thread, &thread_data[i]);
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

static pthread_mutex_t test_lock = PTHREAD_MUTEX_INITIALIZER;

void *test_pthread_mutex_thread(void *arg)
{
	struct spinlock_thread_test_data *thread_data = arg;
	long long i, reps;

	if (!opt_disable_rseq && thread_data->reg
			&& rseq_register_current_thread())
		abort();
	reps = thread_data->reps;
	for (i = 0; i < reps; i++) {
		pthread_mutex_lock(&test_lock);
		test_global_count++;
		pthread_mutex_unlock(&test_lock);
#ifndef BENCHMARK
		if (i != 0 && !(i % (reps / 10)))
			printf("tid %d: count %lld\n", (int) gettid(), i);
#endif
	}
	printf_nobench("tid %d: number of retry: %d, signals delivered: %u\n",
		(int) gettid(), nr_retry, signals_delivered);
	if (rseq_unregister_current_thread())
		abort();
	return NULL;
}

void test_pthread_mutex(void)
{
	const int num_threads = opt_threads;
	int ret;
	long long i;
	uintptr_t sum;
	pthread_t test_threads[num_threads];
	struct spinlock_test_data data;
	struct spinlock_thread_test_data thread_data[num_threads];

	memset(&data, 0, sizeof(data));
	for (i = 0; i < num_threads; i++) {
		thread_data[i].reps = opt_reps;
		if (opt_disable_mod <= 0 || (i % opt_disable_mod))
			thread_data[i].reg = 1;
		else
			thread_data[i].reg = 0;
		thread_data[i].data = &data;
		ret = pthread_create(&test_threads[i], NULL,
			test_pthread_mutex_thread, &thread_data[i]);
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

	sum = test_global_count;

	assert(sum == (uintptr_t)opt_reps * num_threads);
}

void *test_percpu_inc_thread(void *arg)
{
	struct inc_thread_test_data *thread_data = arg;
	struct inc_test_data *data = thread_data->data;
	long long i, reps;

	if (!opt_disable_rseq && thread_data->reg
			&& rseq_register_current_thread())
		abort();
	reps = thread_data->reps;
	for (i = 0; i < reps; i++) {
		int cpu, ret;

#ifndef SKIP_FASTPATH
		/* Try fast path. */
		cpu = rseq_cpu_start();
		ret = rseq_addv(&data->c[cpu].count, 1, cpu);
		if (rseq_likely(!ret))
			goto next;
#endif
	slowpath:
		__attribute__((unused));
		for (;;) {
			/* Fallback on cpu_opv system call. */
			cpu = rseq_current_cpu();
			ret = cpu_op_addv(&data->c[cpu].count, 1, cpu);
			if (rseq_likely(!ret))
				break;
			assert(ret >= 0 || errno == EAGAIN);
		}
	next:
		__attribute__((unused));
#ifndef BENCHMARK
		if (i != 0 && !(i % (reps / 10)))
			printf_verbose("tid %d: count %lld\n", (int) gettid(), i);
#endif
	}
	printf_nobench("tid %d: number of retry: %d, signals delivered: %u\n",
		(int) gettid(), nr_retry, signals_delivered);
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

void *test_percpu_inc_thread_atomic(void *arg)
{
	struct inc_thread_test_data *thread_data = arg;
	struct inc_test_data *data = thread_data->data;
	long long i, reps;

	if (!opt_disable_rseq && thread_data->reg
			&& rseq_register_current_thread())
		abort();
	reps = thread_data->reps;
	for (i = 0; i < reps; i++) {
		int cpu = rseq_current_cpu_raw();

		uatomic_inc(&data->c[cpu].count);

#ifndef BENCHMARK
		if (i != 0 && !(i % (reps / 10)))
			printf("tid %d: count %lld\n", (int) gettid(), i);
#endif
	}
	printf_nobench("tid %d: number of retry: %d, signals delivered: %u\n",
		(int) gettid(), nr_retry, signals_delivered);
	if (rseq_unregister_current_thread())
		abort();
	return NULL;
}

void test_percpu_inc_atomic(void)
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
			test_percpu_inc_thread_atomic, &thread_data[i]);
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

void *test_percpu_cmpxchg_thread_atomic(void *arg)
{
	struct inc_thread_test_data *thread_data = arg;
	struct inc_test_data *data = thread_data->data;
	long long i, reps;

	if (!opt_disable_rseq && thread_data->reg
			&& rseq_register_current_thread())
		abort();
	reps = thread_data->reps;
	for (i = 0; i < reps; i++) {
		int cpu = rseq_current_cpu_raw();
		uintptr_t res, prev = data->c[cpu].count;

		for (;;) {
			res = uatomic_cmpxchg(&data->c[cpu].count,
				prev, prev + 1);
			if (res == prev)
				break;
			prev = res;
		}

#ifndef BENCHMARK
		if (i != 0 && !(i % (reps / 10)))
			printf("tid %d: count %lld\n", (int) gettid(), i);
#endif
	}
	printf_nobench("tid %d: number of retry: %d, signals delivered: %u\n",
		(int) gettid(), nr_retry, signals_delivered);
	if (rseq_unregister_current_thread())
		abort();
	return NULL;
}

void test_percpu_cmpxchg_atomic(void)
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
			test_percpu_cmpxchg_thread_atomic, &thread_data[i]);
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

void *test_atomic_inc_thread(void *arg)
{
	struct inc_thread_test_data *thread_data = arg;
	long long i, reps;

	reps = thread_data->reps;
	for (i = 0; i < reps; i++) {
		uatomic_inc(&test_global_count);

#ifndef BENCHMARK
		if (i != 0 && !(i % (reps / 10)))
			printf("tid %d: count %lld\n", (int) gettid(), i);
#endif
	}
	printf_nobench("tid %d: number of retry: %d, signals delivered: %u\n",
		(int) gettid(), nr_retry, signals_delivered);
	return NULL;
}

void test_atomic_inc(void)
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
			test_atomic_inc_thread, &thread_data[i]);
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

	sum = test_global_count;

	assert(sum == (uintptr_t)opt_reps * num_threads);
}

void *test_baseline_inc_thread(void *arg)
{
	struct inc_thread_test_data *thread_data = arg;
	long long i, reps;

	reps = thread_data->reps;
	for (i = 0; i < reps; i++) {
		test_global_count_volatile++;

#ifndef BENCHMARK
		if (i != 0 && !(i % (reps / 10)))
			printf("tid %d: count %lld\n", (int) gettid(), i);
#endif
	}
	printf_nobench("tid %d: number of retry: %d, signals delivered: %u\n",
		(int) gettid(), nr_retry, signals_delivered);
	return NULL;
}

void test_baseline_inc(void)
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
			test_baseline_inc_thread, &thread_data[i]);
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

	sum = test_global_count_volatile;

	assert(sum == (uintptr_t)opt_reps * num_threads);
}

void *test_atomic_cmpxchg_thread(void *arg)
{
	struct inc_thread_test_data *thread_data = arg;
	long long i, reps;

	reps = thread_data->reps;
	for (i = 0; i < reps; i++) {
		uintptr_t res, prev = test_global_count;

		for (;;) {
			res = uatomic_cmpxchg(&test_global_count,
				prev, prev + 1);
			if (res == prev)
				break;
			prev = res;
		}

#ifndef BENCHMARK
		if (i != 0 && !(i % (reps / 10)))
			printf("tid %d: count %lld\n", (int) gettid(), i);
#endif
	}
	printf_nobench("tid %d: number of retry: %d, signals delivered: %u\n",
		(int) gettid(), nr_retry, signals_delivered);
	return NULL;
}

void test_atomic_cmpxchg(void)
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
			test_atomic_cmpxchg_thread, &thread_data[i]);
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

	sum = test_global_count;

	assert(sum == (uintptr_t)opt_reps * num_threads);
}

int percpu_list_push(struct percpu_list *list, struct percpu_list_node *node)
{
	intptr_t *targetptr, newval, expect;
	int cpu, ret;

#ifndef SKIP_FASTPATH
	/* Try fast path. */
	cpu = rseq_cpu_start();
	/* Load list->c[cpu].head with single-copy atomicity. */
	expect = (intptr_t)RSEQ_READ_ONCE(list->c[cpu].head);
	newval = (intptr_t)node;
	targetptr = (intptr_t *)&list->c[cpu].head;
	node->next = (struct percpu_list_node *)expect;
	ret = rseq_cmpeqv_storev(targetptr, expect, newval, cpu);
	if (rseq_likely(!ret))
		return cpu;
#endif
	/* Fallback on cpu_opv system call. */
slowpath:
	__attribute__((unused));
	for (;;) {
		cpu = rseq_current_cpu();
		/* Load list->c[cpu].head with single-copy atomicity. */
		expect = (intptr_t)RSEQ_READ_ONCE(list->c[cpu].head);
		newval = (intptr_t)node;
		targetptr = (intptr_t *)&list->c[cpu].head;
		node->next = (struct percpu_list_node *)expect;
		ret = cpu_op_cmpeqv_storev(targetptr, expect, newval, cpu);
		if (rseq_likely(!ret))
			break;
		assert(ret >= 0 || errno == EAGAIN);
	}
	return cpu;
}

/*
 * Unlike a traditional lock-less linked list; the availability of a
 * rseq primitive allows us to implement pop without concerns over
 * ABA-type races.
 */
struct percpu_list_node *percpu_list_pop(struct percpu_list *list)
{
	struct percpu_list_node *head;
	int cpu, ret;

#ifndef SKIP_FASTPATH
	/* Try fast path. */
	cpu = rseq_cpu_start();
	ret = rseq_cmpnev_storeoffp_load((intptr_t *)&list->c[cpu].head,
		(intptr_t)NULL,
		offsetof(struct percpu_list_node, next),
		(intptr_t *)&head, cpu);
	if (rseq_likely(!ret))
		return head;
	if (ret > 0)
		return NULL;
#endif
	/* Fallback on cpu_opv system call. */
	slowpath:
		__attribute__((unused));
	for (;;) {
		cpu = rseq_current_cpu();
		ret = cpu_op_cmpnev_storeoffp_load(
			(intptr_t *)&list->c[cpu].head,
			(intptr_t)NULL,
			offsetof(struct percpu_list_node, next),
			(intptr_t *)&head, cpu);
		if (rseq_likely(!ret))
			break;
		if (ret > 0)
			return NULL;
		assert(ret >= 0 || errno == EAGAIN);
	}
	return head;
}

void *test_percpu_list_thread(void *arg)
{
	long long i, reps;
	struct percpu_list *list = (struct percpu_list *)arg;

	if (rseq_register_current_thread())
		abort();

	reps = opt_reps;
	for (i = 0; i < reps; i++) {
		struct percpu_list_node *node = percpu_list_pop(list);

		if (opt_yield)
			sched_yield();  /* encourage shuffling */
		if (node)
			percpu_list_push(list, node);
	}

	if (rseq_unregister_current_thread())
		abort();

	return NULL;
}

/* Simultaneous modification to a per-cpu linked list from many threads.  */
void test_percpu_list(void)
{
	const int num_threads = opt_threads;
	int i, j, ret;
	intptr_t sum = 0, expected_sum = 0;
	struct percpu_list list;
	pthread_t test_threads[num_threads];
	cpu_set_t allowed_cpus;

	memset(&list, 0, sizeof(list));

	/* Generate list entries for every usable cpu. */
	sched_getaffinity(0, sizeof(allowed_cpus), &allowed_cpus);
	for (i = 0; i < CPU_SETSIZE; i++) {
		if (!CPU_ISSET(i, &allowed_cpus))
			continue;
		for (j = 1; j <= 100; j++) {
			struct percpu_list_node *node;

			expected_sum += j;

			node = malloc(sizeof(*node));
			assert(node);
			node->data = j;
			node->next = list.c[i].head;
			list.c[i].head = node;
		}
	}

	for (i = 0; i < num_threads; i++) {
		ret = pthread_create(&test_threads[i], NULL,
			test_percpu_list_thread, &list);
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

	for (i = 0; i < CPU_SETSIZE; i++) {
		cpu_set_t pin_mask;
		struct percpu_list_node *node;

		if (!CPU_ISSET(i, &allowed_cpus))
			continue;

		CPU_ZERO(&pin_mask);
		CPU_SET(i, &pin_mask);
		sched_setaffinity(0, sizeof(pin_mask), &pin_mask);

		while ((node = percpu_list_pop(&list))) {
			sum += node->data;
			free(node);
		}
	}

	/*
	 * All entries should now be accounted for (unless some external
	 * actor is interfering with our allowed affinity while this
	 * test is running).
	 */
	assert(sum == expected_sum);
}

static void test_signal_interrupt_handler(int signo)
{
	signals_delivered++;
}

static int set_signal_handler(void)
{
	int ret = 0;
	struct sigaction sa;
	sigset_t sigset;

	ret = sigemptyset(&sigset);
	if (ret < 0) {
		perror("sigemptyset");
		return ret;
	}

	sa.sa_handler = test_signal_interrupt_handler;
	sa.sa_mask = sigset;
	sa.sa_flags = 0;
	ret = sigaction(SIGUSR1, &sa, NULL);
	if (ret < 0) {
		perror("sigaction");
		return ret;
	}

	printf_nobench("Signal handler set for SIGUSR1\n");

	return ret;
}

static void show_usage(int argc, char **argv)
{
	printf("Usage : %s <OPTIONS>\n",
		argv[0]);
	printf("OPTIONS:\n");
	printf("	[-1 loops] Number of loops for delay injection 1\n");
	printf("	[-2 loops] Number of loops for delay injection 2\n");
	printf("	[-3 loops] Number of loops for delay injection 3\n");
	printf("	[-4 loops] Number of loops for delay injection 4\n");
	printf("	[-5 loops] Number of loops for delay injection 5 (-1 to enable -m)\n");
	printf("	[-6 loops] Number of loops for delay injection 6 (-1 to enable -m)\n");
	printf("	[-7 loops] Number of loops for delay injection 7 (-1 to enable -m)\n");
	printf("	[-8 loops] Number of loops for delay injection 8 (-1 to enable -m)\n");
	printf("	[-9 loops] Number of loops for delay injection 9 (-1 to enable -m)\n");
	printf("	[-m N] Yield/sleep/kill every modulo N (default 0: disabled) (>= 0)\n");
	printf("	[-y] Yield\n");
	printf("	[-k] Kill thread with signal\n");
	printf("	[-s S] S: =0: disabled (default), >0: sleep time (ms)\n");
	printf("	[-f N] Use fallback every N failure (>= 1)\n");
	printf("	[-t N] Number of threads (default 200)\n");
	printf("	[-r N] Number of repetitions per thread (default 5000)\n");
	printf("	[-d] Disable rseq system call (no initialization)\n");
	printf("	[-D M] Disable rseq for each M threads\n");
	printf("	[-T test] Choose test: (b)aseline, percpu (s)pinlock, percpu (l)ist, percpu (i)ncrement, global pthread (M)utex, global counter (I)ncrement, global (C)mpxchg, percpu atomic increment (p), percpu atomic cmpxchg (P)\n");
	printf("	[-h] Show this help.\n");
	printf("\n");
}

int main(int argc, char **argv)
{
	int i;

	if (set_signal_handler())
		goto error;
	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-')
			continue;
		switch (argv[i][1]) {
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			if (argc < i + 2) {
				show_usage(argc, argv);
				goto error;
			}
			loop_cnt[argv[i][1] - '0'] = atol(argv[i + 1]);
			i++;
			break;
		case 'm':
			if (argc < i + 2) {
				show_usage(argc, argv);
				goto error;
			}
			opt_modulo = atol(argv[i + 1]);
			if (opt_modulo < 0) {
				show_usage(argc, argv);
				goto error;
			}
			i++;
			break;
		case 's':
			if (argc < i + 2) {
				show_usage(argc, argv);
				goto error;
			}
			opt_sleep = atol(argv[i + 1]);
			if (opt_sleep < 0) {
				show_usage(argc, argv);
				goto error;
			}
			i++;
			break;
		case 'y':
			opt_yield = 1;
			break;
		case 'k':
			opt_signal = 1;
			break;
		case 'd':
			opt_disable_rseq = 1;
			break;
		case 'D':
			if (argc < i + 2) {
				show_usage(argc, argv);
				goto error;
			}
			opt_disable_mod = atol(argv[i + 1]);
			if (opt_disable_mod < 0) {
				show_usage(argc, argv);
				goto error;
			}
			i++;
			break;
		case 'f':
			if (argc < i + 2) {
				show_usage(argc, argv);
				goto error;
			}
			opt_fallback_cnt = atol(argv[i + 1]);
			if (opt_fallback_cnt < 1) {
				show_usage(argc, argv);
				goto error;
			}
			i++;
			break;
		case 't':
			if (argc < i + 2) {
				show_usage(argc, argv);
				goto error;
			}
			opt_threads = atol(argv[i + 1]);
			if (opt_threads < 0) {
				show_usage(argc, argv);
				goto error;
			}
			i++;
			break;
		case 'r':
			if (argc < i + 2) {
				show_usage(argc, argv);
				goto error;
			}
			opt_reps = atoll(argv[i + 1]);
			if (opt_reps < 0) {
				show_usage(argc, argv);
				goto error;
			}
			i++;
			break;
		case 'h':
			show_usage(argc, argv);
			goto end;
		case 'T':
			if (argc < i + 2) {
				show_usage(argc, argv);
				goto error;
			}
			opt_test = *argv[i + 1];
			switch (opt_test) {
			case 'b':
			case 's':
			case 'l':
			case 'i':
			case 'M':
			case 'I':
			case 'C':
			case 'p':
			case 'P':
				break;
			default:
				show_usage(argc, argv);
				goto error;
			}
			i++;
			break;
		default:
			show_usage(argc, argv);
			goto error;
		}
	}

	if (!opt_disable_rseq && rseq_register_current_thread())
		goto error;
	switch (opt_test) {
	case 's':
		printf_nobench("spinlock\n");
		test_percpu_spinlock();
		break;
	case 'l':
		printf_nobench("linked list\n");
		test_percpu_list();
		break;
	case 'b':
		if (opt_threads > 1) {
			printf("Baseline only works with single thread\n");
			abort();
		}
		printf_nobench("global counter increment (baseline)\n");
		test_baseline_inc();
		break;
	case 'i':
		printf_nobench("counter increment\n");
		test_percpu_inc();
		break;
	case 'M':
		printf_nobench("pthread mutex\n");
		test_pthread_mutex();
		break;
	case 'I':
		printf_nobench("atomic inc\n");
		test_atomic_inc();
		break;
	case 'C':
		printf_nobench("atomic cmpxchg\n");
		test_atomic_cmpxchg();
		break;
	case 'p':
		printf_nobench("percpu atomic inc\n");
		test_percpu_inc_atomic();
		break;
	case 'P':
		printf_nobench("percpu atomic cmpxchg\n");
		test_percpu_cmpxchg_atomic();
	}
	if (rseq_unregister_current_thread())
		abort();
end:
	return 0;

error:
	return -1;
}
