#include <dlfcn.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void (*linked_lib_autoreg_fn)(void);
static void (*linked_lib2_autoreg_fn)(void);

int main(int argc, char **argv)
{
	void *handle1, *handle2;

	handle1 = dlopen("./libtest-linked-lib.so",
			RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE);
	assert(handle1);
	linked_lib_autoreg_fn = dlsym(handle1, "linked_lib_autoreg_fn");
	assert(linked_lib_autoreg_fn);

	linked_lib_autoreg_fn();

	handle2 = dlopen("./libtest-linked-lib2.so",
			RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE);
	assert(handle2);
	linked_lib2_autoreg_fn = dlsym(handle2, "linked_lib2_autoreg_fn");
	assert(linked_lib2_autoreg_fn);

	linked_lib_autoreg_fn();
	linked_lib2_autoreg_fn();

	return 0;
}
