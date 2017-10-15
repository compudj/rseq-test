#define _GNU_SOURCE

#define TESTNAME	do_test_linked_lib

#include "test-template.h"

void linked_lib_fn(void)
{
	do_test_linked_lib();
}
