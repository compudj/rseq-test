#define _GNU_SOURCE

#define TESTNAME	do_test_linked_lib

#include "test-template.h"

/* Own state, not shared with other libs. */
static pthread_key_t rseq_key;
static __thread int rseq_registered;

void linked_lib_fn(void)
{
	do_test_linked_lib();
}

void linked_lib_autoreg_fn(void)
{
	fprintf(stderr, "%s\n", __func__);
	if (!rseq_registered) {
		if (!rseq_register_current_thread()) {
			/*
			 * Register destroy notifier. Pointer needs to
			 * be non-NULL.
			 */
			if (pthread_setspecific(rseq_key, (void *)0x1))
				abort();
		} else {
			if (errno != EBUSY)
				abort();
		}
		rseq_registered = 1;
	}
	linked_lib_fn();
}

static void destroy_rseq_key(void *key)
{
	fprintf(stderr, "%s\n", __func__);
	if (rseq_unregister_current_thread())
		abort();
	rseq_registered = 0;
}

static void __attribute__((constructor)) lib_init(void)
{
	int ret;

	fprintf(stderr, "%s\n", __func__);
	ret = pthread_key_create(&rseq_key, destroy_rseq_key);
	if (ret) {
		errno = -ret;
		perror("pthread_key_create");
		abort();
	}
}

/*
 * Cannot be called when threads are still using the key.
 * Requires RTLD_NODELETE.
 */
static void __attribute__((destructor)) lib_destroy(void)
{
	int ret;

	fprintf(stderr, "%s\n", __func__);
	ret = pthread_key_delete(rseq_key);
	if (ret) {
		errno = -ret;
		perror("pthread_key_delete");
		abort();
	}
}
