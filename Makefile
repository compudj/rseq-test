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

CPPFLAGS = -O2 -g -I./
LDFLAGS = -L./ -pthread

all: example-rseq-cpuid example-rseq-cpuid-lazy test-rseq-cpuid \
	benchmark-rseq librseq.so libcpu-op.so libtest-linked-lib.so \
	libtest-linked-lib2.so test-use-lib \
	test-dlopen test-dlopen-dlclose

REMOTE_INCLUDES=remote/*.h

librseq.so: fetch remote/rseq.c ${REMOTE_INCLUDES}
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) -shared -fpic remote/rseq.c -Wl,rpath=./ -o $@

libcpu-op.so: fetch remote/cpu-op.c ${REMOTE_INCLUDES}
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) -shared -fpic remote/cpu-op.c -Wl,rpath=./ -o $@

example-rseq-cpuid: example-rseq-cpuid.c librseq.so ${REMOTE_INCLUDES}
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) $< -lrseq -o $@

example-rseq-cpuid-lazy: example-rseq-cpuid-lazy.c ${REMOTE_INCLUDES} librseq.so
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) $< -lrseq -o $@

test-rseq-cpuid: test-rseq-cpuid.c ${REMOTE_INCLUDES} librseq.so
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) $< -lrseq -o $@

benchmark-rseq: benchmark-rseq.c ${REMOTE_INCLUDES} librseq.so libcpu-op.so
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
		test-dlopen-dlclose
	rm -rf remote/
