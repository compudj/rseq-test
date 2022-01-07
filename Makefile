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

CFLAGS += -O2 -g -I./ -I./remote/
LDFLAGS += -L./ -pthread -Wl,-rpath=./

all: example-rseq-cpuid example-rseq-cpuid-lazy test-rseq-cpuid \
	librseq.so \
	test-rseq-adaptative-lock benchmark-rseq-adaptative-lock

remote/rseq.c: fetch

REMOTE_INCLUDES=$(wildcard remote/*.h)

librseq.so: remote/rseq.c ${REMOTE_INCLUDES}
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) -shared -fpic remote/rseq.c -o $@

example-rseq-cpuid: example-rseq-cpuid.c librseq.so ${REMOTE_INCLUDES}
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) $< -lrseq -o $@

example-rseq-cpuid-lazy: example-rseq-cpuid-lazy.c ${REMOTE_INCLUDES} librseq.so
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) $< -lrseq -o $@

test-rseq-cpuid: test-rseq-cpuid.c ${REMOTE_INCLUDES} librseq.so
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) $< -lrseq -o $@

test-rseq-adaptative-lock: test-rseq-adaptative-lock.c ${REMOTE_INCLUDES} librseq.so
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) $< -lrseq -o $@

benchmark-rseq-adaptative-lock: test-rseq-adaptative-lock.c ${REMOTE_INCLUDES} librseq.so
	$(CC) $(CFLAGS) $(LDFLAGS) $(CPPFLAGS) -DBENCHMARK $< -lrseq -o $@

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
