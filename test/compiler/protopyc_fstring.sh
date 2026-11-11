#!/usr/bin/env bash
# f-string fields keep their conversion and format spec in a module built
# from protopyc's C++ (the generated code formatted every field as str()).
#
# usage: protopyc_fstring.sh <protopyc> <protopy> <probe.py>
set -eu
protopyc=$1 protopy=$2 probe=$3

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"
cp "$probe" fstring_probe.py
"$protopyc" fstring_probe.py --build-so >build.log 2>&1 || { cat build.log; exit 1; }
mv module.so fstring_probe.so
rm fstring_probe.py
(cd / && timeout 60 "$protopy" -p "$tmp" -c 'import fstring_probe as m
got = m.fields(3.14159, "ab", 255)
want = "3.14|   ab|0xff|11111111|3.14159|'"'"'ab'"'"'|3.14159|0.30000000000000004|F<>|F<x>|000255|x=3.14159"
assert got == want, (got, want)
print("protopyc fstring OK")')
