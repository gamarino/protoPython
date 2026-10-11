#!/usr/bin/env bash
# A module compiled by protopyc runs generators and classes as the interpreter
# runs the same source: generator parameters and locals across yields, send(),
# throw(), close(), a return value in StopIteration, yield from, yields inside
# loops, branches and try statements; and functions and methods that refer to
# the module's own names (a class's methods naming the class) when they are
# called from another module.
#
# The interpreter's output on the source is the expected output. The compiled
# module must print exactly that under a heap limit that forces collections
# while generators are suspended (submission at every safepoint, then the
# default threshold) and under protopy's default adaptive heap controller.
#
# Before the fix: compiled generators read their parameters as None (and a
# yield inside a loop did not compile), and a method that named its class
# raised NameError when called from another module.
#
# usage: protopyc_generators_classes.sh <protopyc> <protopy> <probe.py> <driver.py> <work dir>
set -eu
protopyc=$1 protopy=$2 probe=$3 driver=$4 work=$5

rm -rf "$work"
mkdir -p "$work/src" "$work/mod" "$work/run"
cp "$probe" "$work/src/generators_classes_probe.py"
cp "$driver" "$work/src/driver.py"
cp "$driver" "$work/run/driver.py"

ulimit -c 0
(cd "$work/src" && timeout 300 "$protopy" driver.py >"$work/expected.txt" 2>&1) || {
    cat "$work/expected.txt"; echo "FAILED: the interpreter run"; exit 1; }
[ "$(tail -n 1 "$work/expected.txt")" = END ] || {
    cat "$work/expected.txt"; echo "FAILED: the interpreter run is incomplete"; exit 1; }

(cd "$work/src" && "$protopyc" generators_classes_probe.py --build-so >build.log 2>&1) || {
    cat "$work/src/build.log"; echo "FAILED: protopyc"; exit 1; }
mv "$work/src/module.so" "$work/mod/generators_classes_probe.so"

run() {
    echo "== $*"
    # Only the compiled module is importable from run/.
    (cd "$work/run" && env "$@" timeout 300 "$protopy" -p "$work/mod" driver.py) \
        >"$work/actual.txt" 2>&1 || true
    if ! diff -u "$work/expected.txt" "$work/actual.txt"; then
        echo "FAILED under: $*"; exit 1
    fi
}
run PROTOCORE_HEAP_LIMIT_CELLS=1500000 PROTOCORE_GC_CONTEXT_THRESHOLD=200
run PROTOCORE_HEAP_LIMIT_CELLS=1500000
run PROTOPY_TEST_CONFIG=default
echo "ALL OK"
