#!/usr/bin/env bash
# A module built from protopyc's C++ computes with 64-bit integers and promotes
# overflowing products to big integers (the emitted code once assumed a 64-bit
# `long` and GCC's __builtin_mul_overflow).
#
# usage: protopyc_portable_ints.sh <protopyc> <protopy> <probe.py>
set -eu
protopyc=$1 protopy=$2 probe=$3

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"
cp "$probe" portable_ints_probe.py
"$protopyc" portable_ints_probe.py --build-so >build.log 2>&1 || { cat build.log; exit 1; }
mv module.so portable_ints_probe.so
rm portable_ints_probe.py
(cd / && "$protopy" -p "$tmp" -c 'import portable_ints_probe as m
assert m.BIG == 2 ** 80, m.BIG
assert m.SMALL == -42, m.SMALL
assert m.POW == 3 ** 39, m.POW
assert m.AREA == 6, m.AREA
assert m.mul(3037000500, 3037000500) == 3037000500 ** 2
print("protopyc portable integers OK")')
