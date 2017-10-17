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

all: example-rseq-cpuid example-rseq-cpuid-lazy test-rseq-cpuid \
	benchmark-rseq librseq.so libcpu-op.so libtest-linked-lib.so \
	libtest-linked-lib2.so test-use-lib test-use-lib-define-tls-sym \
	test-dlopen

example-rseq-cpuid: example-rseq-cpuid.c rseq.c rseq.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -pthread -o $@ example-rseq-cpuid.c rseq.c

example-rseq-cpuid-lazy: example-rseq-cpuid-lazy.c rseq.c rseq.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -pthread -o $@ example-rseq-cpuid-lazy.c rseq.c

test-rseq-cpuid: test-rseq-cpuid.c rseq.c rseq.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -pthread -o $@ test-rseq-cpuid.c rseq.c

benchmark-rseq: benchmark-rseq.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -DBENCHMARK -pthread -o $@ benchmark-rseq.c rseq.c cpu-op.c

librseq.so: rseq.c rseq.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -shared -fpic -o $@ $<

libcpu-op.so: cpu-op.c cpu-op.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -shared -fpic -o $@ $<

libtest-linked-lib.so: test-linked-lib.c rseq.h cpu-op.h test-template.h librseq.so libcpu-op.so
	$(CC) $(CFLAGS) $(CPPFLAGS) -shared -fpic -o $@ $< -L./ -lrseq -lcpu-op

libtest-linked-lib2.so: test-linked-lib2.c rseq.h cpu-op.h test-template.h librseq.so libcpu-op.so
	$(CC) $(CFLAGS) $(CPPFLAGS) -shared -fpic -o $@ $< -L./ -lrseq -lcpu-op

test-use-lib: test-use-lib.c rseq.h cpu-op.h test-template.h librseq.so libcpu-op.so libtest-linked-lib.so libtest-linked-lib2.so
	$(CC) $(CFLAGS) $(CPPFLAGS) -pthread -o $@ test-use-lib.c -L./ -lrseq -lcpu-op \
		-ltest-linked-lib -ltest-linked-lib2

test-use-lib-define-tls-sym: test-use-lib-define-tls-sym.c rseq.h cpu-op.h test-template.h librseq.so libcpu-op.so libtest-linked-lib.so libtest-linked-lib2.so
	$(CC) $(CFLAGS) $(CPPFLAGS) -pthread -o $@ test-use-lib-define-tls-sym.c -L./ -lrseq -lcpu-op \
		-ltest-linked-lib -ltest-linked-lib2

test-dlopen: test-dlopen.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -pthread -o $@ test-dlopen.c -ldl

.PHONY: clean

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
		test-use-lib-define-tls-sym \
		test-dlopen
