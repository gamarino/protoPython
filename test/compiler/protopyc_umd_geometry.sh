#!/usr/bin/env bash
# The errors of protoCore's example geometry module (a C++ UMD module) are
# caught by the handlers of a module compiled by protopyc as by the
# interpreter's: test/regression/umd_geometry_errors.py is compiled to a
# shared module and imported by a one-line driver run through
# protopy_umd_host, which registers the module's provider.  The compiled
# module prints the same checks and "umd_geometry_errors: PASS".
#
# usage: protopyc_umd_geometry.sh <protopyc> <protopy_umd_host> <test.py> <work dir>
set -eu
protopyc=$1 host=$2 source=$3 work=$4

rm -rf "$work"
mkdir -p "$work/src" "$work/mod" "$work/run"
cp "$source" "$work/src/umd_geometry_errors.py"
echo "import umd_geometry_errors" >"$work/run/driver.py"

ulimit -c 0
(cd "$work/src" && "$protopyc" umd_geometry_errors.py --build-so >build.log 2>&1) || {
    cat "$work/src/build.log"; echo "FAILED: protopyc"; exit 1; }
mv "$work/src/module.so" "$work/mod/umd_geometry_errors.so"

# Only the compiled module is importable from run/.
(cd "$work/run" && timeout 300 "$host" -p "$work/mod" driver.py) 2>&1
