#!/usr/bin/env bash
# A module compiled by protopyc propagates exceptions, runs `with` statements
# and gives the methods of classes defined in functions their closure, as the
# interpreter does on the same source: a statement that raises stops its
# statement list; finally clauses run on exceptions, returns, break and
# continue; except clauses match, bare `raise` re-raises; __enter__/__exit__
# get the right arguments and a true __exit__ result suppresses; a method
# called through an instance sees the variables of the function that defined
# its class.
#
# The interpreter's output on the source is the expected output for
# exceptions_with_driver.py. Where the interpreter does not run a finally
# clause that an exception or a return leaving an except clause must run,
# exceptions_with_cpython_driver.py checks the compiled module against CPython
# 3's output instead (exceptions_with_cpython.expected). The compiled module
# must print exactly that under a heap limit that forces collections
# (submission at every safepoint, then the default threshold) and under
# protopy's default adaptive heap controller.
#
# Before the fix: a raising statement did not stop the rest of its statement
# list (and a raised exception class was not instantiated), `with` called
# neither __enter__ nor __exit__, and such methods read their closure
# variables as None.
#
# usage: protopyc_exceptions_with.sh <protopyc> <protopy> <probe.py> <driver.py>
#        <cpython driver.py> <cpython expected> <work dir>
set -eu
protopyc=$1 protopy=$2 probe=$3 driver=$4 cdriver=$5 cexpected=$6 work=$7

rm -rf "$work"
mkdir -p "$work/src" "$work/mod" "$work/run"
cp "$probe" "$work/src/exceptions_with_probe.py"
cp "$driver" "$work/src/driver.py"
cp "$driver" "$work/run/driver.py"
cp "$cdriver" "$work/run/cdriver.py"

ulimit -c 0
(cd "$work/src" && timeout 300 "$protopy" driver.py >"$work/expected.txt" 2>&1) || {
    cat "$work/expected.txt"; echo "FAILED: the interpreter run"; exit 1; }
[ "$(tail -n 1 "$work/expected.txt")" = END ] || {
    cat "$work/expected.txt"; echo "FAILED: the interpreter run is incomplete"; exit 1; }

(cd "$work/src" && "$protopyc" exceptions_with_probe.py --build-so >build.log 2>&1) || {
    cat "$work/src/build.log"; echo "FAILED: protopyc"; exit 1; }
mv "$work/src/module.so" "$work/mod/exceptions_with_probe.so"

run() {
    echo "== $*"
    # Only the compiled module is importable from run/.
    (cd "$work/run" && env "$@" timeout 300 "$protopy" -p "$work/mod" driver.py) \
        >"$work/actual.txt" 2>&1 || true
    if ! diff -u "$work/expected.txt" "$work/actual.txt"; then
        echo "FAILED under: $*"; exit 1
    fi
    (cd "$work/run" && env "$@" timeout 300 "$protopy" -p "$work/mod" cdriver.py) \
        >"$work/actual_cpython.txt" 2>&1 || true
    if ! diff -u "$cexpected" "$work/actual_cpython.txt"; then
        echo "FAILED (CPython reference) under: $*"; exit 1
    fi
}
run PROTOCORE_HEAP_LIMIT_CELLS=1500000 PROTOCORE_GC_CONTEXT_THRESHOLD=200
run PROTOCORE_HEAP_LIMIT_CELLS=1500000
run PROTOPY_TEST_CONFIG=default
echo "ALL OK"
