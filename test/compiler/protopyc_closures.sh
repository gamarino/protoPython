#!/usr/bin/env bash
# A module compiled by protopyc closes over variables as CPython does and
# binds call arguments as CPython does.  Closures hold cells, not snapshots:
# a read sees the variable's current binding (late binding), `nonlocal` and
# `del` reach the defining scope, an empty cell raises NameError or
# UnboundLocalError, a recursive nested def calls itself (its name rebound or
# decorated, it calls what the name holds), class bodies and their methods
# see the enclosing function's variables, zero-argument super() works in a
# class defined in a function and from __init_subclass__.  Calls bind
# positional and keyword arguments, defaults evaluated once at the
# definition, keyword-only and positional-only parameters, *args and
# **kwargs, with CPython's TypeErrors.
#
# The expected output is CPython 3's on the same source (closures.expected).
# The interpreter must print it on the source, and the compiled module must
# print it under a heap limit that forces collections (submission at every
# safepoint, then the default threshold) and under protopy's default adaptive
# heap controller.
#
# Before the fix: free variables were snapshots taken at the definition
# (late binding, nonlocal and del were lost; a recursive nested def read its
# own name as None), and a compiled def bound positional arguments only
# (defaults re-evaluated at every call, keywords, *args and **kwargs dropped).
#
# usage: protopyc_closures.sh <protopyc> <protopy> <probe.py> <driver.py>
#        <expected> <work dir>
set -eu
protopyc=$1 protopy=$2 probe=$3 driver=$4 expected=$5 work=$6

rm -rf "$work"
mkdir -p "$work/src" "$work/mod" "$work/run"
cp "$probe" "$work/src/closures_probe.py"
cp "$driver" "$work/src/driver.py"
cp "$driver" "$work/run/driver.py"

ulimit -c 0
(cd "$work/src" && timeout 300 "$protopy" driver.py >"$work/interpreted.txt" 2>&1) || true
if ! diff -u "$expected" "$work/interpreted.txt"; then
    echo "FAILED: the interpreter run"; exit 1
fi

(cd "$work/src" && "$protopyc" closures_probe.py --build-so >build.log 2>&1) || {
    cat "$work/src/build.log"; echo "FAILED: protopyc"; exit 1; }
mv "$work/src/module.so" "$work/mod/closures_probe.so"

run() {
    echo "== $*"
    # Only the compiled module is importable from run/.
    (cd "$work/run" && env "$@" timeout 300 "$protopy" -p "$work/mod" driver.py) \
        >"$work/actual.txt" 2>&1 || true
    if ! diff -u "$expected" "$work/actual.txt"; then
        echo "FAILED under: $*"; exit 1
    fi
}
run PROTOCORE_HEAP_LIMIT_CELLS=1500000 PROTOCORE_GC_CONTEXT_THRESHOLD=200
run PROTOCORE_HEAP_LIMIT_CELLS=1500000
run PROTOPY_TEST_CONFIG=default
echo "ALL OK"
