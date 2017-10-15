#define _GNU_SOURCE

#define TESTNAME	do_test_use_lib

#include "test-template.h"

void linked_lib_fn(void);

int main(int argc, char **argv)
{
	if (rseq_register_current_thread())
		abort();

	/* Perform test in main executable, using librseq and libcpu_op. */
	do_test_use_lib();

	/* Perform test within library, itself using librseq and libcpu_op. */
	linked_lib_fn();

	if (rseq_unregister_current_thread())
		abort();

	return 0;
}
