#!/usr/bin/env bash
# A module compiled by protopyc reads attributes, defines classes and binds
# methods as CPython does: `obj.name` applies properties, descriptors,
# __getattribute__ and __getattr__ and raises AttributeError for a missing
# name; a compiled class is a Python class (its own __mro__, __init_subclass__,
# a base for classes the interpreter defines, a context manager for the
# interpreter's `with`); and a function or lambda used as a method keeps its
# closure however it reached the class.
#
# The expected output is CPython 3's on the same source
# (python_classes.expected). The interpreter must print it on the source, and
# the compiled module must print it under a heap limit that forces collections
# (submission at every safepoint, then the default threshold) and under
# protopy's default adaptive heap controller.
#
# Before the fix: a missing attribute read as None, properties and other
# descriptors were not applied, a compiled class had no __mro__ of its own (an
# interpreted subclass did not inherit its methods), the interpreter's `with`
# called its __enter__ without the instance, and a lambda used as a method read
# its free variables as None.
#
# usage: protopyc_python_classes.sh <protopyc> <protopy> <probe.py> <driver.py>
#        <expected> <work dir>
set -eu
protopyc=$1 protopy=$2 probe=$3 driver=$4 expected=$5 work=$6

rm -rf "$work"
mkdir -p "$work/src" "$work/mod" "$work/run"
cp "$probe" "$work/src/python_classes_probe.py"
cp "$driver" "$work/src/driver.py"
cp "$driver" "$work/run/driver.py"

ulimit -c 0
(cd "$work/src" && timeout 300 "$protopy" driver.py >"$work/interpreted.txt" 2>&1) || true
if ! diff -u "$expected" "$work/interpreted.txt"; then
    echo "FAILED: the interpreter run"; exit 1
fi

(cd "$work/src" && "$protopyc" python_classes_probe.py --build-so >build.log 2>&1) || {
    cat "$work/src/build.log"; echo "FAILED: protopyc"; exit 1; }
mv "$work/src/module.so" "$work/mod/python_classes_probe.so"

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
