#define _GNU_SOURCE
#include <assert.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>
#include <stdlib.h>

#include "cpu-op.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static int do_test_max_ops(void *dst, void *src, size_t len,
		void *dst2, void *src2, size_t len2)
{
	struct cpu_op opvec[] = {
		[0] = {
			.op = CPU_MEMCPY_OP,
			.len = len,
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.dst, dst),
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.src, src),
			.u.memcpy_op.expect_fault_dst = 0,
			.u.memcpy_op.expect_fault_src = 0,
		},
		[1] = {
			.op = CPU_MEMCPY_OP,
			.len = len2,
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.dst, dst2),
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.src, src2),
			.u.memcpy_op.expect_fault_dst = 0,
			.u.memcpy_op.expect_fault_src = 0,
		},
		[2] = {
			.op = CPU_MEMCPY_OP,
			.len = len2,
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.dst, dst2),
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.src, src2),
			.u.memcpy_op.expect_fault_dst = 0,
			.u.memcpy_op.expect_fault_src = 0,
		},
		[3] = {
			.op = CPU_MEMCPY_OP,
			.len = len2,
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.dst, dst2),
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.src, src2),
			.u.memcpy_op.expect_fault_dst = 0,
			.u.memcpy_op.expect_fault_src = 0,
		},
		[4] = {
			.op = CPU_MEMCPY_OP,
			.len = len2,
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.dst, dst2),
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.src, src2),
			.u.memcpy_op.expect_fault_dst = 0,
			.u.memcpy_op.expect_fault_src = 0,
		},
		[5] = {
			.op = CPU_MEMCPY_OP,
			.len = len2,
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.dst, dst2),
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.src, src2),
			.u.memcpy_op.expect_fault_dst = 0,
			.u.memcpy_op.expect_fault_src = 0,
		},
		[6] = {
			.op = CPU_MEMCPY_OP,
			.len = len2,
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.dst, dst2),
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.src, src2),
			.u.memcpy_op.expect_fault_dst = 0,
			.u.memcpy_op.expect_fault_src = 0,
		},
		[7] = {
			.op = CPU_MEMCPY_OP,
			.len = len2,
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.dst, dst2),
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.src, src2),
			.u.memcpy_op.expect_fault_dst = 0,
			.u.memcpy_op.expect_fault_src = 0,
		},
		[8] = {
			.op = CPU_MEMCPY_OP,
			.len = len2,
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.dst, dst2),
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.src, src2),
			.u.memcpy_op.expect_fault_dst = 0,
			.u.memcpy_op.expect_fault_src = 0,
		},
		[9] = {
			.op = CPU_MEMCPY_OP,
			.len = len2,
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.dst, dst2),
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.src, src2),
			.u.memcpy_op.expect_fault_dst = 0,
			.u.memcpy_op.expect_fault_src = 0,
		},
		[10] = {
			.op = CPU_MEMCPY_OP,
			.len = len2,
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.dst, dst2),
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.src, src2),
			.u.memcpy_op.expect_fault_dst = 0,
			.u.memcpy_op.expect_fault_src = 0,
		},
		[11] = {
			.op = CPU_MEMCPY_OP,
			.len = len2,
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.dst, dst2),
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.src, src2),
			.u.memcpy_op.expect_fault_dst = 0,
			.u.memcpy_op.expect_fault_src = 0,
		},
		[12] = {
			.op = CPU_MEMCPY_OP,
			.len = len2,
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.dst, dst2),
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.src, src2),
			.u.memcpy_op.expect_fault_dst = 0,
			.u.memcpy_op.expect_fault_src = 0,
		},
		[13] = {
			.op = CPU_MEMCPY_OP,
			.len = len2,
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.dst, dst2),
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.src, src2),
			.u.memcpy_op.expect_fault_dst = 0,
			.u.memcpy_op.expect_fault_src = 0,
		},
		[14] = {
			.op = CPU_MEMCPY_OP,
			.len = len2,
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.dst, dst2),
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.src, src2),
			.u.memcpy_op.expect_fault_dst = 0,
			.u.memcpy_op.expect_fault_src = 0,
		},
		[15] = {
			.op = CPU_MEMCPY_OP,
			.len = len2,
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.dst, dst2),
			LINUX_FIELD_u32_u64_INIT_ONSTACK(.u.memcpy_op.src, src2),
			.u.memcpy_op.expect_fault_dst = 0,
			.u.memcpy_op.expect_fault_src = 0,
		},
	};
	int cpu, ret;

	cpu = cpu_op_get_current_cpu();
	ret = cpu_opv(opvec, ARRAY_SIZE(opvec), cpu, 0);
	if (ret) {
		perror("cpu_opv");
		abort();
	}
	return ret;
}

#define NR_ARRAY	16384
#define NR_ITER		(NR_ARRAY * 1024)
#define ARRAY_LEN	4096

#define ARRAY2_LEN	8

char tmp1[NR_ARRAY][ARRAY_LEN], tmp2[NR_ARRAY][ARRAY_LEN];
char tmp3[NR_ARRAY][ARRAY2_LEN], tmp4[NR_ARRAY][ARRAY2_LEN];

int main(int argc, char **argv)
{
	int ret = 0;
	uint64_t i;

	for (i = 0; i < NR_ITER; i++) {
		uint64_t idx = i % NR_ARRAY;

		ret |= do_test_max_ops(tmp1[idx], tmp2[idx], ARRAY_LEN,
			tmp3[idx], tmp4[idx], ARRAY2_LEN);
	}

	return ret;
}
