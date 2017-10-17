#include <dlfcn.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <poll.h>

#include "rseq.h"

static void (*linked_lib_fn)(void);

int main(int argc, char **argv)
{
	void *handle1, *handle2;
	int i, ret;

	if (rseq_register_current_thread())
		abort();
	for (i = 0; i < 10000; i++) {
		handle1 = dlopen("./libtest-linked-lib.so",
				RTLD_NOW | RTLD_GLOBAL);
		assert(handle1);
		linked_lib_fn = dlsym(handle1, "linked_lib_fn");
		assert(linked_lib_fn);

		linked_lib_fn();

		/*
		 * Need to clear rseq_cs before lib unload.
	 	 */
		rseq_prepare_unload();

		ret = dlclose(handle1);
		assert(!ret);

		handle2 = dlopen("./libtest-linked-lib2.so",
				RTLD_NOW | RTLD_GLOBAL);
		assert(handle2);
		ret = dlclose(handle2);
		assert(!ret);

		poll(NULL, 0, 1);	/* Sleep 1ms */
	}
	if (rseq_unregister_current_thread())
		abort();

	return 0;
}
