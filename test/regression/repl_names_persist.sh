#!/usr/bin/env bash
# usage: repl_names_persist.sh <protopy> <project-version> <work-dir>
#
# REPL statements run in __main__, so names bound by assignments, def and import
# stay visible on later lines; and every user-visible version string (REPL
# banner, help(), --help, sys.version) reports the CMake project version.
#
# runRepl appends to $HOME/.protopy_history, so HOME points at <work-dir> (a
# directory in the build tree, created if missing and never deleted here).
set -u
protopy="$1"
version="$2"
workdir="$3"

mkdir -p -- "$workdir" || { echo "FAIL: cannot create $workdir"; exit 1; }
export HOME="$workdir"

out=$(printf 'x = 5\nprint(x + 1)\ndef f():\n    return x * 2\n\nprint(f())\nimport math\nprint(math.pi > 3)\nprint(__name__)\nhelp()\n' \
    | "$protopy" -i 2>&1)

fail() {
    echo "FAIL: $1"
    echo "--- output ---"
    echo "$out"
    exit 1
}

case "$out" in *Error*) fail "unexpected exception";; esac
for want in "6" "10" "True" "__main__" \
            "protoPython $version (Python 3.14 compatible)" \
            "Welcome to protoPython $version help!"; do
    grep -qF -- "$want" <<<"$out" || fail "missing: $want"
done
grep -qF "HPy Integrated" <<<"$out" && fail "stale banner text"
grep -qF "0.1.0" <<<"$out" && fail "stale version 0.1.0"

out=$("$protopy" --help 2>&1)
grep -qF "protopy $version - protoPython runtime" <<<"$out" || fail "--help does not report $version"

out=$("$protopy" -c 'import sys; print(sys.version)' 2>&1)
grep -qF "(protoPython $version," <<<"$out" || fail "sys.version does not report $version"

echo "OK"
exit 0
