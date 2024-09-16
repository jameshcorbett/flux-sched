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

test_expect_success 'an ssd jobspec can be allocated' '
	flux module remove sched-simple &&
	flux module remove resource &&
	flux config load <<EOF &&
[resource]
noverify = true
norestrict = true
path="${SHARNESS_TEST_SRCDIR}/R.json"
EOF
	flux module load resource monitor-force-up &&
	flux module load sched-fluxion-resource &&
	flux module load sched-fluxion-qmanager &&
	flux module list &&
	flux queue start --all --quiet &&
	flux resource list &&
	flux resource status &&
	jobid=$(flux job submit --flags=waitable \
			${SHARNESS_TEST_SRCDIR}/ssd-jobspec.json) &&
	flux job wait-event -vt5 ${jobid} alloc &&
	flux job wait-event -vt5 ${jobid} clean
'

test_expect_success 'single-node non-ssd jobspecs can be allocated' '
	jobid=$(flux submit -n1 -N1 true) &&
	flux job wait-event -vt5 ${jobid} alloc &&
	flux job wait-event -vt5 ${jobid} clean
'

test_expect_success 'a second ssd jobspec can be allocated' '
	jobid=$(flux job submit --flags=waitable \
			${SHARNESS_TEST_SRCDIR}/ssd-jobspec.json) &&
	flux job wait-event -vt15 ${jobid} alloc &&
	flux job wait-event -vt5 ${jobid} clean
'

test_expect_success 'single-node non-ssd jobspecs can still be allocated' '
	(flux cancel ${jobid} || true) &&
	jobid=$(flux submit -n1 -N1 true) &&
	flux job wait-event -vt5 ${jobid} alloc &&
	flux job wait-event -vt5 ${jobid} clean
'

test_expect_success 'unload fluxion' '
	flux module remove sched-fluxion-qmanager &&
	flux module remove sched-fluxion-resource
'

test_done
