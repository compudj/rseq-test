# Copyright (C) 2016  Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
#
# THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
# OR IMPLIED. ANY USE IS AT YOUR OWN RISK.
#
# Permission is hereby granted to use or copy this program for any
# purpose, provided the above notices are retained on all copies.
# Permission to modify the code and to distribute modified code is
# granted, provided the above notices are retained, and a notice that
# the code was modified is included with the above copyright notice.

CPPFLAGS += -O2 -g -I./ -I./remote/
LDFLAGS += -L./ -pthread -Wl,-rpath=./

all: example-rseq-cpuid example-rseq-cpuid-lazy test-rseq-cpuid \
	benchmark-rseq librseq.so libcpu-op.so libtest-linked-lib.so \
	libtest-linked-lib2.so test-use-lib \
	test-dlopen test-dlopen-dlclose test-many-rseq \
	test-membarrier-global test-cpu-opv test-rseq-progress \
	test-rseq-adaptative-lock benchmark-rseq-adaptative-lock

remote/rseq.c: fetch
remote/cpu-op.c: fetch

REMOTE_INCLUDES=$(wildcard remote/*.h)

librseq.so: remote/rseq.c ${REMOTE_INCLUDES}
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) -shared -fpic remote/rseq.c -o $@

libcpu-op.so: remote/cpu-op.c ${REMOTE_INCLUDES}
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) -shared -fpic remote/cpu-op.c -o $@

example-rseq-cpuid: example-rseq-cpuid.c librseq.so ${REMOTE_INCLUDES}
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) $< -lrseq -o $@

example-rseq-cpuid-lazy: example-rseq-cpuid-lazy.c ${REMOTE_INCLUDES} librseq.so
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) $< -lrseq -o $@

test-rseq-cpuid: test-rseq-cpuid.c ${REMOTE_INCLUDES} librseq.so
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) $< -lrseq -o $@

benchmark-rseq: benchmark-rseq.c ${REMOTE_INCLUDES} librseq.so libcpu-op.so
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) -DBENCHMARK $< -lrseq -lcpu-op -o $@

test-many-rseq: test-many-rseq.c ${REMOTE_INCLUDES} librseq.so libcpu-op.so
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) $< -lrseq -lcpu-op -o $@

test-rseq-progress: test-rseq-progress.c ${REMOTE_INCLUDES} librseq.so libcpu-op.so
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) $< -lrseq -lcpu-op -o $@

test-rseq-adaptative-lock: test-rseq-adaptative-lock.c ${REMOTE_INCLUDES} librseq.so libcpu-op.so
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) $< -lrseq -lcpu-op -o $@

benchmark-rseq-adaptative-lock: test-rseq-adaptative-lock.c ${REMOTE_INCLUDES} librseq.so libcpu-op.so
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) -DBENCHMARK $< -lrseq -lcpu-op -o $@

libtest-linked-lib.so: test-linked-lib.c test-template.h ${REMOTE_INCLUDES} librseq.so libcpu-op.so
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) -shared -fpic $< -lrseq -lcpu-op -o $@

libtest-linked-lib2.so: test-linked-lib2.c test-template.h ${REMOTE_INCLUDES} librseq.so libcpu-op.so
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) -shared -fpic $< -lrseq -lcpu-op -o $@

test-use-lib: test-use-lib.c test-template.h ${REMOTE_INCLUDES} librseq.so libcpu-op.so libtest-linked-lib.so libtest-linked-lib2.so
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) $< -lrseq -lcpu-op \
		-ltest-linked-lib -ltest-linked-lib2 -o $@

test-use-lib-define-tls-sym: test-use-lib-define-tls-sym.c test-template.h ${REMOTE_INCLUDES} librseq.so libcpu-op.so libtest-linked-lib.so libtest-linked-lib2.so
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) $< -lrseq -lcpu-op \
		-ltest-linked-lib -ltest-linked-lib2 -o $@

test-dlopen: test-dlopen.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) $< -ldl -o $@

test-dlopen-dlclose: test-dlopen-dlclose.c librseq.so
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) $< -ldl -lrseq -o $@

test-membarrier-global: test-membarrier-global.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) $< -o $@

test-cpu-opv: test-cpu-opv.c
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) $< -lcpu-op -o $@

.PHONY: clean fetch

fetch:
	./fetch.sh

clean:
	rm -f benchmark-cpuid-rseq \
		benchmark-cpuid-rseq-lazy \
		benchmark-cpuid-baseline \
		benchmark-cpuid-glibc-getcpu \
		benchmark-cpuid-inline-getcpu \
		benchmark-cpuid-syscall \
		benchmark-cpuid-gs \
		benchmark-rseq \
		example-rseq-cpuid \
		example-rseq-cpuid-lazy \
		test-rseq-cpuid \
		librseq.so \
		libcpu-op.so \
		test-use-lib \
		libtest-linked-lib.so \
		libtest-linked-lib2.so \
		test-dlopen \
		test-dlopen-dlclose \
		test-membarrier-global \
		test-cpu-opv \
		test-rseq-progress \
		test-rseq-adaptative-lock \
		benchmark-rseq-adaptative-lock
	rm -rf remote/
