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
#include <urcu/uatomic.h>
#include <urcu/futex.h>
#include <urcu/arch.h>
#include <urcu/compiler.h>

#include <rseq.h>

static inline pid_t gettid(void)
{
	return syscall(__NR_gettid);
}

uintptr_t test_global_count;
static long long opt_reps = 5000;
static int opt_disable_rseq, opt_disable_mod = 0, opt_threads = 200;

struct rseq_lock {
	unsigned int state;	/* enum rseq_lock_state */
	int cpu;
	int spins;
};

enum rseq_lock_state {
	RSEQ_LOCK_STATE_UNLOCKED = 0,
	RSEQ_LOCK_STATE_LOCKED = 1,
	RSEQ_LOCK_STATE_LOCKED_CONTENDED = 2,
};

struct rseq_lock testlock = {
	.state = RSEQ_LOCK_STATE_UNLOCKED,
	.cpu = -1,
	.spins = 0,
};

static int sys_rseq(struct rseq *rseq_abi, uint32_t rseq_len,
		int flags, uint32_t sig)
{
        return syscall(__NR_rseq, rseq_abi, rseq_len, flags, sig);
}

#ifndef BENCHMARK

#define printf_nobench(fmt, ...)	printf(fmt, ## __VA_ARGS__)

int uncontended_lock_taken, contended_lock_taken, failures, reached_limit;

#else

#define printf_nobench(fmt, ...)

#endif /* BENCHMARK */

#define ASM_RSEQ_CS_FLAG_ABORT_AT_IP	(1U << 3)

/* x86_64 */
static inline __attribute__((always_inline))
int rseq_trylock(struct rseq_lock *lock)
{
	int spins = 0, fetch_spins, max_spins;

	fetch_spins = RSEQ_READ_ONCE(lock->spins);
	max_spins = fetch_spins * 2 + 10;

	__asm__ __volatile__ goto (
		__RSEQ_ASM_DEFINE_TABLE(3, 0x0, ASM_RSEQ_CS_FLAG_ABORT_AT_IP, 1f, (2f - 1f), 4f) /* start, post_commit_offset, abort */
		"movl %[expect], %%eax\n\t"
		"lock; cmpxchgl %[newv], %[v]\n\t"
		"jz 6f\n\t"	/* Got uncontended lock. */

		/* Handle lock contention with adaptative busy-spinning and futex. */

		/* Start rseq by storing table entry pointer into rseq_cs. */
		RSEQ_ASM_STORE_RSEQ_CS(1, 3b, RSEQ_CS_OFFSET(%[rseq_abi]))
		"5:\n\t"
		"movl %[expect], %%eax\n\t"
		/*
		 * Store with possible side-effect, but not last instruction.
		 * Sets the ZF on success.
		 */
		"lock; cmpxchgl %[newv], %[v]\n\t"
		"60:\n\t"	/* instruction after cmpxchg for which abort needs to check ZF. */
		/*
		 * cmpxchg is not the last instruction of this rseq. This
		 * requires carefully handling the state of ZF, and comparing
		 * its state on abort of abort-at-ip at label 60 to check if
		 * cmpxchg has succeded.
		 */
		"jz %l[contended_lock_taken]\n\t"	/* Got lock. */
		/*
		 * Compare lock owner cpu id to current cpu id. If the lock
		 * owner grabbed the lock on the same cpu as ours, chances
		 * are it's sitting on that CPU runqueue awaiting for us
		 * to call futex wait and let it proceed. Don't busy-wait in
		 * that scenario.
		 */
		"movq %[lock_owner_cpu], %%rcx\n\t"
		"cmpq " __rseq_str(RSEQ_CPU_ID_OFFSET(%[rseq_abi])) ", %%rcx\n\t"
		"jz %l[abort]\n\t"
		"incl %[spins]\n\t"
		"cmpl %[spins], %[max_spins]\n\t"
		"je %l[spins_limit]\n\t"
		"rep; nop\n\t"	/* cpu_relax for busy loop. */
		"jmp 5b\n\t"	/* retry */
		"2:\n\t"
		RSEQ_ASM_DEFINE_ABORT(4,
			/* Got lock if ZF is set and abort-at-ip == 60b. */
			/* abort-at-ip must be pop from the stack. */
			"popq %%rcx\n\t"	/* pop does not affect flags. */
			"jz 7f\n\t"
			"addq $128, %%rsp\n\t"	/* x86-64 redzone */
			"jmp 8f\n\t"
			"7:\n\t"
			"addq $128, %%rsp\n\t"	/* x86-64 redzone */
			"lea 60b(%%rip), %%rax\n\t"
			"cmpq %%rcx, %%rax\n\t"
			"jz %l[contended_lock_taken]\n\t"	/* Got lock. */
			"8:\n\t",
			abort)
		"6:\n\t"
		: /* gcc asm goto does not allow outputs */
		: [rseq_abi]		"r" (&__rseq_abi),
		  [v]			"m" (lock->state),
		  [lock_owner_cpu]	"m" (lock->cpu),
		  [spins]		"m" (spins),
		  [max_spins]		"r" (max_spins),
		  [expect]		"i" (RSEQ_LOCK_STATE_UNLOCKED),
		  [newv]		"r" (RSEQ_LOCK_STATE_LOCKED)
		: "memory", "cc", "rax", "rcx"
		  RSEQ_INJECT_CLOBBER
		: contended_lock_taken, abort, spins_limit
	);
	/* Uncontended lock taken */
#ifndef BENCHMARK
	uatomic_inc(&uncontended_lock_taken);
#endif
	return 0;
contended_lock_taken:
	/*
	 * On contended lock taken, move spins target towards the number of
	 * spins loops that led us to succeed.
	 */
	RSEQ_WRITE_ONCE(lock->spins, fetch_spins + ((spins - fetch_spins) / 8));
#ifndef BENCHMARK
	uatomic_inc(&contended_lock_taken);
#endif
	return 0;
abort:
	/*
	 * On abort, the best decision would have been _not_ to spin at all.
	 * Move spins target towards 0.
	 */
	RSEQ_WRITE_ONCE(lock->spins, fetch_spins - (fetch_spins / 8));
#ifndef BENCHMARK
	uatomic_inc(&failures);
#endif
	return 1;
spins_limit:
	/*
	 * When reaching spins limit, move spins target towards that limit.
	 */
	RSEQ_WRITE_ONCE(lock->spins, fetch_spins + ((spins - fetch_spins) / 8));
#ifndef BENCHMARK
	uatomic_inc(&reached_limit);
#endif
	return 1;
}

static
void rseq_lock_slowpath(struct rseq_lock *lock)
{
	for (;;) {
		unsigned int state;

		state = uatomic_cmpxchg(&lock->state, RSEQ_LOCK_STATE_UNLOCKED,
				RSEQ_LOCK_STATE_LOCKED_CONTENDED);
		switch (state) {
		case RSEQ_LOCK_STATE_UNLOCKED:
			return;	/* Got lock. */
		case RSEQ_LOCK_STATE_LOCKED:
			state = uatomic_cmpxchg(&lock->state,
					RSEQ_LOCK_STATE_LOCKED,
					RSEQ_LOCK_STATE_LOCKED_CONTENDED);
			if (state == RSEQ_LOCK_STATE_UNLOCKED) {
				caa_cpu_relax();
				continue;	/* Retry. */
			}
			break;	/* State is contended. */
		case RSEQ_LOCK_STATE_LOCKED_CONTENDED:
			break;	/* State is contended. */
		}
		while (futex(&lock->state, FUTEX_WAIT,
				RSEQ_LOCK_STATE_LOCKED_CONTENDED,
				NULL, NULL, 0)) {
			switch (errno) {
			case EWOULDBLOCK:
				/* Value already changed. */
				goto skip_wait;
			case EINTR:
				/* Retry futex wait if interrupted by signal. */
				break;
			default:
				/* Unexpected error. */
				abort();
			}
		}
skip_wait:
		;
	}
}

/* Do a fair comparison with glibc with a function call */
__attribute__((noinline))
void rseq_lock(struct rseq_lock *lock)
{
	if (caa_unlikely(rseq_trylock(&testlock)))
		rseq_lock_slowpath(&testlock);
	CMM_STORE_SHARED(lock->cpu, __rseq_abi.cpu_id_start);
}

__attribute__((noinline))
int rseq_unlock(struct rseq_lock *lock)
{
	unsigned int state;

	CMM_STORE_SHARED(lock->cpu, -1);
	state = uatomic_xchg(&lock->state, RSEQ_LOCK_STATE_UNLOCKED);
	if (caa_unlikely(state == RSEQ_LOCK_STATE_LOCKED_CONTENDED)) {
		/*
		 * Only awaken a single thread. It will itself set the lock state
		 * to RSEQ_LOCK_STATE_LOCKED_CONTENDED on success, which
		 * ensures its own unlock will awaken another thread, or if
		 * another thread races to get the lock (non-contended) first,
		 * the thread we have awakened will set the lock state to
		 * RSEQ_LOCK_STATE_LOCKED_CONTENDED as well, and go back to
		 * waiting on the futex. This ensures the non-contended thread
		 * that owns the lock will perform another wakeup.
		 */
		if (futex(&lock->state, FUTEX_WAKE, 1, NULL, NULL, 0) < 0)
			abort();
	}
}

struct adaptative_lock_thread_test_data {
	long long reps;
	int reg;
};

void *test_adaptative_lock_thread(void *arg)
{
	struct adaptative_lock_thread_test_data *thread_data = arg;
	long long i, reps;

	if (!opt_disable_rseq && thread_data->reg
			&& rseq_register_current_thread())
		abort();
	reps = thread_data->reps;
	for (i = 0; i < reps; i++) {
		int ret;

		rseq_lock(&testlock);
		test_global_count++;
		rseq_unlock(&testlock);
#ifndef BENCHMARK
		if (i != 0 && !(i % (reps / 10)))
			printf("tid: %d: count: %lld spins: %d uncontended lock taken: %d contended lock taken: %d failures: %d limit: %d\n",
				(int) gettid(), i,
				RSEQ_READ_ONCE(testlock.spins),
				RSEQ_READ_ONCE(uncontended_lock_taken),
				RSEQ_READ_ONCE(contended_lock_taken),
				RSEQ_READ_ONCE(failures),
				RSEQ_READ_ONCE(reached_limit));
#endif
	}
	if (rseq_unregister_current_thread())
		abort();
	return NULL;
}

void test_adaptative_lock(void)
{
	const int num_threads = opt_threads;
	int ret;
	long long i;
	uintptr_t sum;
	pthread_t test_threads[num_threads];
	struct adaptative_lock_thread_test_data thread_data[num_threads];

	if (sys_rseq(NULL, 0, RSEQ_FLAG_QUERY_ABORT_AT_IP, 0) != 0) {
		perror("sys_rseq abort-at-ip extension not supported");
		abort();
	}

	for (i = 0; i < num_threads; i++) {
		thread_data[i].reps = opt_reps;
		if (opt_disable_mod <= 0 || (i % opt_disable_mod))
			thread_data[i].reg = 1;
		else
			thread_data[i].reg = 0;
		ret = pthread_create(&test_threads[i], NULL,
			test_adaptative_lock_thread, &thread_data[i]);
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

static void show_usage(int argc, char **argv)
{
	printf("Usage : %s <OPTIONS>\n",
		argv[0]);
	printf("OPTIONS:\n");
	printf("	[-t N] Number of threads (default 200)\n");
	printf("	[-r N] Number of repetitions per thread (default 5000)\n");
	printf("	[-d] Disable rseq system call (no initialization)\n");
	printf("	[-D M] Disable rseq for each M threads\n");
	printf("	[-h] Show this help.\n");
	printf("\n");
}

int main(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-')
			continue;
		switch (argv[i][1]) {
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
		default:
			show_usage(argc, argv);
			goto error;
		}
	}
	test_adaptative_lock();
end:
	return 0;
error:
	return -1;
}
