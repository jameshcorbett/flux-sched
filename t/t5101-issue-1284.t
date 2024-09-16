#!/bin/sh
#
test_description='Ensure fluxion cancels ssds as expected'

. `dirname $0`/sharness.sh

if test_have_prereq ASAN; then
    skip_all='skipping issues tests under AddressSanitizer'
    test_done
fi
SIZE=1
test_under_flux ${SIZE}

test_expect_success 'make sure jobspec exists' '
	echo ${SHARNESS_TEST_SRCDIR} &&
	ls -l ${SHARNESS_TEST_SRCDIR}/ssd-jobspec.json &&
	cat ${SHARNESS_TEST_SRCDIR}/ssd-jobspec.json
'

test_expect_success 'test that issue #1283 is resolved' '
	flux module remove sched-simple &&
	flux module remove resource &&
	flux config load <<EOF &&
[resource]
noverify = true
norestrict = true
path="/home/corbett8/Desktop/Flux/flux-sched/t/R.json"
EOF
	flux module load resource monitor-force-up &&
	flux module load sched-fluxion-resource &&
	flux module load sched-fluxion-qmanager &&
	flux module list &&
	flux queue start --all --quiet &&
	flux resource list &&
	flux resource status &&
	jobid1=$(flux job submit --flags=waitable \
			${SHARNESS_TEST_SRCDIR}/ssd-jobspec.json) &&
	jobid2=$(flux job submit --flags=waitable \
			${SHARNESS_TEST_SRCDIR}/ssd-jobspec.json) &&
	flux jobs -a &&
	flux job wait-event -vt15 ${jobid2} alloc &&
	flux job wait -av &&
	jobid3=$(flux job submit --flags=waitable \
			${SHARNESS_TEST_SRCDIR}/ssd-jobspec.json) &&
	flux jobs -a &&
	sleep 1 && flux jobs -a &&
	flux job eventlog $jobid1 &&
	flux job eventlog $jobid2 &&
	flux job eventlog $jobid3
'

test_expect_success 'unload fluxion' '
	flux module remove sched-fluxion-qmanager &&
	flux module remove sched-fluxion-resource && false
'

test_done
