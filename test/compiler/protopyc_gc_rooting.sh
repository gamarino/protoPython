#!/usr/bin/env bash
# protopyc-generated code keeps every live object where the collector sees it
# (protoPython issue #2): compiled functions with allocating loops, closures,
# comprehensions, exceptions and lambdas, and a module-level loop, give the
# right answers when collections run during them.
#
# Before the fix compiled code ran in its caller's context, kept locals and
# temporaries only in C++ variables, and called safepoint() on that context:
# wrong results and SIGSEGV under any heap limit, including the adaptive heap
# controller's default.
#
# usage: protopyc_gc_rooting.sh <protopyc> <protopy> <probe.py> <driver.py> <work dir>
set -eu
protopyc=$1 protopy=$2 probe=$3 driver=$4 work=$5

rm -rf "$work"
mkdir -p "$work/mod"
cd "$work"
cp "$probe" gc_rooting_probe.py
"$protopyc" gc_rooting_probe.py --build-so >build.log 2>&1 || { cat build.log; exit 1; }
mv module.so mod/gc_rooting_probe.so
# Only the compiled module may be importable.
rm gc_rooting_probe.py
cp "$driver" driver.py

ulimit -c 0
run() {
    echo "== $*"
    (cd "$work" && env "$@" timeout 300 "$protopy" -p "$work/mod" driver.py) || {
        echo "FAILED under: $*"; exit 1; }
}
# Submission at every safepoint, under a ceiling that forces many cycles.
run PROTOCORE_HEAP_LIMIT_CELLS=1500000 PROTOCORE_GC_CONTEXT_THRESHOLD=200
# The same ceiling with protoCore's default submission threshold.
run PROTOCORE_HEAP_LIMIT_CELLS=1500000
# The default configuration (adaptive heap controller).
run PROTOPY_TEST_CONFIG=default
