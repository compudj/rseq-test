#include <dlfcn.h>
#include <assert.h>

static void (*linked_lib_fn)(void);
static void (*linked_lib2_fn)(void);

int main(int argc, char **argv)
{
	void *handle1, *handle2;

	handle1 = dlopen("./libtest-linked-lib.so", RTLD_NOW | RTLD_GLOBAL);
	assert(handle1);
	linked_lib_fn = dlsym(handle1, "linked_lib_fn");
	assert(linked_lib_fn);

	linked_lib_fn();

	handle2 = dlopen("./libtest-linked-lib2.so", RTLD_NOW | RTLD_GLOBAL);
	assert(handle2);
	linked_lib2_fn = dlsym(handle2, "linked_lib2_fn");
	assert(linked_lib2_fn);

	linked_lib2_fn();

	return 0;
}
