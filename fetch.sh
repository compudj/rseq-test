#!/bin/sh

BASEURL=raw.githubusercontent.com/compudj/linux-percpu-dev/rseq/dev-local/
BASEOUTPUT=remote

curl -s -o ${BASEOUTPUT}/include/rseq.h  --create-dirs \
	https://${BASEURL}/tools/testing/selftests/rseq/rseq.h
curl -s -o ${BASEOUTPUT}/include/rseq-arm.h  --create-dirs \
	https://${BASEURL}/tools/testing/selftests/rseq/rseq-arm.h
curl -s -o ${BASEOUTPUT}/include/rseq-x86.h  --create-dirs \
	https://${BASEURL}/tools/testing/selftests/rseq/rseq-ppc.h
curl -s -o ${BASEOUTPUT}/include/rseq-ppc.h  --create-dirs \
	https://${BASEURL}/tools/testing/selftests/rseq/rseq-x86.h

curl -s -o ${BASEOUTPUT}/include/cpu-op.h  --create-dirs \
	https://${BASEURL}/tools/testing/selftests/cpu-opv/cpu-op.h

curl -s -o ${BASEOUTPUT}/rseq.c  --create-dirs \
	https://${BASEURL}/tools/testing/selftests/rseq/rseq.c
curl -s -o ${BASEOUTPUT}/cpu-op.c  --create-dirs \
	https://${BASEURL}/tools/testing/selftests/cpu-opv/cpu-op.c
