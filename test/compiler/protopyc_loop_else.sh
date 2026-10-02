#!/usr/bin/env bash
# break / continue in a loop's else clause target the enclosing loop in a
# module built from protopyc's C++ (the generated else clause was inside the
# C++ loop it belongs after).
#
# usage: protopyc_loop_else.sh <protopyc> <protopy> <probe.py>
set -eu
protopyc=$1 protopy=$2 probe=$3

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"
cp "$probe" loop_else_probe.py
"$protopyc" loop_else_probe.py --build-so >build.log 2>&1 || { cat build.log; exit 1; }
mv module.so loop_else_probe.so
rm loop_else_probe.py
(cd / && timeout 60 "$protopy" -p "$tmp" -c 'import loop_else_probe as m
assert m.for_else_break() == 1, m.for_else_break()
assert m.for_else_continue() == [], m.for_else_continue()
assert m.while_else_break() == 1, m.while_else_break()
assert m.while_else_continue() == [], m.while_else_continue()
assert m.inner_break_skips_else() == [1, 2], m.inner_break_skips_else()
assert m.else_runs_on_exhaustion() == [1, 2, 3], m.else_runs_on_exhaustion()
print("protopyc loop else OK")')
