#!/bin/sh

test_description='Test that JGF execution target ranks are reconciled with R

R assigns execution targets by position: the Nth host of
.execution.nodelist is the host of the Nth lowest rank in .execution.R_lite.
The JGF in .scheduling carries its own rank per vertex, and fluxion reconciles
the two when it builds the resource graph. These tests cover the cases that
reconciliation has to get right: a host that owns several ranks (every host
does, in an instance running more than one broker per node), a storage_node
owning an execution target just as a node does, and a vertex of some other
type that merely happens to be named like a host.
'

. $(dirname $0)/sharness.sh

# module-nopanic lets a failed `flux module load` return an error instead of
# taking the broker -- and thus the rest of this file -- down with it.
test_under_flux 2 full -Sbroker.module-nopanic=1

query="${SHARNESS_BUILD_DIRECTORY}/resource/utilities/resource-query"

# Usage: jgf_from_r R-file > jgf-file
# Emit the full resource graph of an R as JGF. This is the same graph fluxion
# would build from that R with the rv1exec reader, so the ranks it carries
# agree with R by construction and each test below perturbs only what it
# means to test.
jgf_from_r() {
	printf "find status=up\nquit\n" | \
	    ${query} -L $1 -f rv1exec -F jgf -S CA -P high | head -1
}

# Usage: load_fluxion
# The stats RPC is the sync point: `flux module load` returns before mod_main
# has built the resource graph, so without it a graph that fluxion rejects
# still looks like a successful load here.
load_fluxion() {
	load_resource &&
	flux module stats sched-fluxion-resource >/dev/null
}

# Usage: load_jgf jgf-file
# Install jgf-file as R's .scheduling key and load fluxion against it.
load_jgf() {
	jq --slurpfile jgf $1 ".scheduling = \$jgf[0]" base.R >reload.R &&
	flux kvs put resource.R="$(cat reload.R)" &&
	flux dmesg -C &&
	flux module reload resource &&
	load_fluxion
}

# Usage: config_jgf jgf-file
# As load_jgf, for the configured instance below. The KVS route is not open
# there: with a [[resource.config]] table the resource module regenerates R
# from the config on every reload, so .scheduling has to arrive by the file
# that [resource] scheduling names.
config_jgf() {
	cp $1 sched.json &&
	flux dmesg -C &&
	flux module reload resource &&
	load_fluxion
}

test_expect_success 'unload sched-simple' '
	flux module remove -f sched-simple
'

#
# A host that owns several ranks. Every host does when an instance runs more
# than one broker per node, which is exactly how this test instance runs: its
# nodelist names one host twice, once for rank 0 and once for rank 1.
#

test_expect_success 'this instance puts two ranks on one host' '
	flux kvs get resource.R >base.R &&
	test $(jq -r ".execution.nodelist[0]" base.R | flux hostlist -c) -eq 2 &&
	test $(jq -r ".execution.nodelist[0]" base.R | sort -u | wc -l) -eq 1
'

test_expect_success 'its JGF has one node vertex per rank' '
	jgf_from_r base.R >multirank.jgf &&
	test $(jq "[.graph.nodes[] |
	    select(.metadata.type == \"node\")] | length" multirank.jgf) -eq 2 &&
	test $(jq "[.graph.nodes[] |
	    select(.metadata.type == \"node\") |
	    .metadata.paths.containment] | unique | length" multirank.jgf) -eq 1
'

test_expect_success 'fluxion loads a JGF whose host owns two ranks' '
	load_jgf multirank.jgf &&
	load_qmanager &&
	test $(flux resource list -s free -no {nnodes}) -eq 2
'

test_expect_success 'a job can use both ranks' '
	run_timeout 60 flux run -N2 -n2 hostname
'

test_expect_success 'unload fluxion' '
	remove_qmanager &&
	remove_resource
'

test_expect_success 'a JGF rank the host does not own is rejected' '
	jq "(.graph.nodes[] | select(.metadata.rank == 1) | .metadata.rank) = 7" \
	    multirank.jgf >badrank.jgf &&
	test_must_fail load_jgf badrank.jgf &&
	flux dmesg | grep reconcile_rank
'

test_expect_success 'a JGF vertex with no rank under such a host is rejected' '
	jq "del(.graph.nodes[] | select(.metadata.rank == 1) | .metadata.rank)" \
	    multirank.jgf >norank.jgf &&
	test_must_fail load_jgf norank.jgf &&
	flux dmesg | grep reconcile_rank
'

#
# One rank per host, so that reconciliation can assign a rank rather than
# only check it. Renaming the hosts also makes them distinguishable, which
# the cases below need.
#

test_expect_success 'reconfigure the instance with one host per rank' '
	cat >resource.toml <<-EOF &&
	[resource]
	noverify = true
	norestrict = true

	[[resource.config]]
	hosts = "fake[0-1]"
	cores = "0-1"
	EOF
	flux config load resource.toml &&
	flux module reload resource &&
	flux kvs get resource.R >base.R &&
	test "$(jq -r ".execution.nodelist[0]" base.R)" = "fake[0-1]" &&
	jgf_from_r base.R >onerank.jgf
'

test_expect_success 'point the configuration at a JGF file' '
	cp onerank.jgf sched.json &&
	sed -e "s|^\[resource\]$|[resource]\nscheduling = \"${PWD}/sched.json\"|" \
	    resource.toml >resource-jgf.toml &&
	flux config load resource-jgf.toml &&
	flux module reload resource
'

test_expect_success 'fluxion loads a JGF with one rank per host' '
	config_jgf onerank.jgf &&
	test $(flux resource list -s free -no {nnodes}) -eq 2 &&
	remove_resource
'

test_expect_success 'a JGF rank that contradicts R is rejected' '
	jq "(.graph.nodes[] |
	    select(.metadata.paths.containment == \"/cluster0/fake1\") |
	    .metadata.rank) = 0" onerank.jgf >wrongrank.jgf &&
	test_must_fail config_jgf wrongrank.jgf &&
	flux dmesg | grep "rank disagreement for hostname=fake1"
'

#
# storage_node owns an execution target just as node does, so its rank is
# reconciled on the same terms.
#

test_expect_success 'a storage_node vertex is reconciled like a node' '
	jq "(.graph.nodes[] |
	    select(.metadata.paths.containment == \"/cluster0/fake1\") |
	    .metadata.type) = \"storage_node\"" onerank.jgf >storage.jgf &&
	config_jgf storage.jgf &&
	remove_resource
'

test_expect_success 'a storage_node rank that contradicts R is rejected' '
	jq "(.graph.nodes[] |
	    select(.metadata.paths.containment == \"/cluster0/fake1\") |
	    .metadata) += {type: \"storage_node\", rank: 0}" \
	    onerank.jgf >badstorage.jgf &&
	test_must_fail config_jgf badstorage.jgf &&
	flux dmesg | grep "rank disagreement for hostname=fake1"
'

#
# Only a node or a storage_node owns an execution target, so a vertex is
# anchored on the nearest one containing it -- not on whichever component of
# its containment path happens to spell a hostname.
#

test_expect_success "a vertex named after another host takes its hosts rank" '
	jq "(.graph.nodes[] |
	    select(.metadata.paths.containment == \"/cluster0/fake0/core1\") |
	    .metadata) += {basename: \"fake\", id: 1,
	        paths: {containment: \"/cluster0/fake0/fake1\"}}" \
	    onerank.jgf >namesake.jgf &&
	jq -e "[.graph.nodes[] |
	    select(.metadata.paths.containment == \"/cluster0/fake0/fake1\") |
	    .metadata.rank] == [0]" namesake.jgf >/dev/null &&
	config_jgf namesake.jgf &&
	test $(flux resource list -s free -no {nnodes}) -eq 2 &&
	remove_resource
'

test_done
