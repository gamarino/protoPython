#!/usr/bin/env bash
# A module protopyc compiled defines named nested functions in a loop of calls
# (plain, recursive, nonlocal, recursion through a comprehension, a returned
# closure) under a fixed 2.5 M-cell heap: closures hold cells, and a recursive
# nested def calls itself without capturing its name, so a call leaves no cycle
# of mutable objects behind and the loop runs in a bounded heap.
#
# usage: protopyc_nested_def_memory.sh <protopyc> <protopy> <probe.py> <work dir>
set -eu
protopyc=$1 protopy=$2 probe=$3 work=$4

rm -rf "$work"
mkdir -p "$work/src" "$work/mod" "$work/run"
cp "$probe" "$work/src/nested_def_probe.py"
printf 'import nested_def_probe\nnested_def_probe.run()\n' > "$work/run/driver.py"
ulimit -c 0
(cd "$work/src" && "$protopyc" nested_def_probe.py --build-so >build.log 2>&1) || {
    cat "$work/src/build.log"; echo "FAILED: protopyc"; exit 1; }
mv "$work/src/module.so" "$work/mod/nested_def_probe.so"
out=$(cd "$work/run" && env PROTOCORE_HEAP_LIMIT_CELLS=2500000 PROTOCORE_ADAPTIVE_HEAP=0 \
      timeout 600 "$protopy" -p "$work/mod" driver.py 2>&1) || true
echo "$out"
if [ "$out" != "nested def bounded memory OK" ]; then echo "FAILED"; exit 1; fi
echo "ALL OK"
