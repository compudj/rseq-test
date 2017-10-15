#define _GNU_SOURCE

#define TESTNAME	do_test_use_lib

#include "test-template.h"

__thread volatile struct rseq __rseq_abi = {
	.u.e.cpu_id = -1,
};

void linked_lib_fn(void);
void linked_lib2_fn(void);

int main(int argc, char **argv)
{
	if (rseq_register_current_thread(&__rseq_abi))
		abort();

	/* Perform test in main executable, using librseq and libcpu_op. */
	do_test_use_lib();

	/* Perform test within library, itself using librseq and libcpu_op. */
	linked_lib_fn();
	/* Same with 2nd lib. */
	linked_lib2_fn();

	if (rseq_unregister_current_thread(&__rseq_abi))
		abort();

	return 0;
}
