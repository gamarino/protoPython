#!/usr/bin/env bash
# protoCore's rule E3 in code protopyc generates: test/exceptions/
# exception_identity_probe.py is compiled to a shared module and
# test_exception_identity runs its paths with only the compiled module on the
# module path, checking that a ProtoException subclass leaves unchanged.
#
# usage: protopyc_exception_identity.sh <protopyc> <test_exception_identity> <probe.py> <work dir>
set -eu
protopyc=$1 runner=$2 probe=$3 work=$4

rm -rf "$work"
mkdir -p "$work/src" "$work/mod"
cp "$probe" "$work/src/exception_identity_probe.py"
ulimit -c 0
(cd "$work/src" && "$protopyc" exception_identity_probe.py --build-so >build.log 2>&1) || {
    cat "$work/src/build.log"; echo "FAILED: protopyc"; exit 1; }
mv "$work/src/module.so" "$work/mod/exception_identity_probe.so"
cd "$work/mod" && timeout 300 "$runner" -p "$work/mod" 2>&1
