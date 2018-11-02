#!/bin/sh

CC=gcc
CFLAGS="${CFLAGS} -L./ -I./ -I./remote -pthread -Wall -O2 -g"
LDFLAGS="${LDFLAGS} -L./ -pthread -Wl,-rpath=./"

RUNLIST="benchmark-cpuid-rseq
benchmark-cpuid-rseq-lazy
benchmark-cpuid-baseline
benchmark-cpuid-glibc-getcpu
benchmark-cpuid-inline-getcpu
benchmark-cpuid-syscall
benchmark-cpuid-gs"

${CC} ${CPPFLAGS} ${CFLAGS} ${LDFLAGS} -DCONFIG_RSEQ -o benchmark-cpuid-rseq benchmark-cpuid.c -lrseq
${CC} ${CPPFLAGS} ${CFLAGS} ${LDFLAGS} -DCONFIG_RSEQ_LAZY -o benchmark-cpuid-rseq-lazy benchmark-cpuid.c -lrseq
${CC} ${CPPFLAGS} ${CFLAGS} ${LDFLAGS} -DCONFIG_BASELINE -o benchmark-cpuid-baseline benchmark-cpuid.c -lrseq
${CC} ${CPPFLAGS} ${CFLAGS} ${LDFLAGS} -DCONFIG_GLIBC_GETCPU -o benchmark-cpuid-glibc-getcpu benchmark-cpuid.c -lrseq
${CC} ${CPPFLAGS} ${CFLAGS} ${LDFLAGS} -DCONFIG_INLINE_GETCPU -o benchmark-cpuid-inline-getcpu benchmark-cpuid.c -lrseq
${CC} ${CPPFLAGS} ${CFLAGS} ${LDFLAGS} -DCONFIG_GETCPU_SYSCALL -o benchmark-cpuid-syscall benchmark-cpuid.c -lrseq
${CC} ${CPPFLAGS} ${CFLAGS} ${LDFLAGS} -DCONFIG_GETCPU_GS -o benchmark-cpuid-gs benchmark-cpuid.c -lrseq

for a in ${RUNLIST}; do
	echo "Running ./${a} 10 0"
	./${a} 10 0
done

#for a in ${RUNLIST}; do
#	echo "Running ./${a} 10 10"
#	./${a} 10 10
#done
#
#for a in ${RUNLIST}; do
#	echo "Running ./${a} 10 100"
#	./${a} 10 100
#done
