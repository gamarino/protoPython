#!/usr/bin/env bash
# The Makefile written by `protopyc --emit-make` must build a working module on
# this machine: include and library directories come from the build tree, every
# directory exists, the module carries an RPATH, and PROTOPYC_* overrides apply.
#
# usage: protopyc_makefile_paths.sh <protopyc> <protopy> <protoPython include dir> <libprotoPython dir>
set -eu
protopyc=$1 protopy=$2 include_dir=$3 library_dir=$4

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"
printf 'def add_one(n):\n    return n + 1\n\nVALUE = add_one(41)\n' > pp_makefile_probe.py

fail() { echo "FAIL: $1"; echo "--- Makefile ---"; cat Makefile; exit 1; }

"$protopyc" pp_makefile_probe.py --emit-make >/dev/null
grep -qF -- "-I$include_dir" Makefile || fail "missing include directory $include_dir"
grep -qF -- "-L$library_dir" Makefile || fail "missing library directory $library_dir"
grep -qF -- "-Wl,-rpath,$library_dir" Makefile || fail "missing rpath $library_dir"
for dir in $(sed -n 's/^\(INCLUDES\|LDFLAGS\) =//p' Makefile | tr ' ' '\n' | sed -n 's/^-[IL]//p'); do
  [ -d "$dir" ] || fail "directory does not exist: $dir"
done

# Build with the generated Makefile and import the module from another directory.
make -s >/dev/null || fail "make failed"
mv module.so pp_makefile_probe.so
(cd / && "$protopy" -p "$tmp" -c 'import pp_makefile_probe as m
assert m.VALUE == 42, m.VALUE
assert m.add_one(1) == 2') || fail "generated module did not import and run"

# Environment overrides replace the configured values.
PROTOPYC_INCLUDE_DIRS=/opt/pp/include:/opt/pp/core PROTOPYC_LIBRARY_DIRS=/opt/pp/lib PROTOPYC_CXX=/opt/pp/c++ \
  "$protopyc" pp_makefile_probe.py --emit-make >/dev/null
grep -qF -- "INCLUDES = -I/opt/pp/include -I/opt/pp/core" Makefile || fail "PROTOPYC_INCLUDE_DIRS ignored"
grep -qF -- "LDFLAGS = -L/opt/pp/lib -Wl,-rpath,/opt/pp/lib" Makefile || fail "PROTOPYC_LIBRARY_DIRS ignored"
grep -qF -- "CXX = /opt/pp/c++" Makefile || fail "PROTOPYC_CXX ignored"

# Paths make cannot represent are rejected.
if PROTOPYC_INCLUDE_DIRS="/opt/with space" "$protopyc" pp_makefile_probe.py --emit-make >/dev/null 2>&1; then
  fail "a directory containing a space was accepted"
fi
echo "protopyc Makefile paths OK"
