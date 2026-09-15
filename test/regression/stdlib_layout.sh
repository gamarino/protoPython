#!/usr/bin/env bash
# protopy must find its standard library in the installed layout, relative to
# the executable, from any working directory; a ../lib/python3.14 next to the
# working directory must not replace it; and the PROTO_PYTHONPATH marker must
# not become a module search path.
#
# usage: stdlib_layout.sh <protopy> <stdlib install parent, e.g. lib/protoPython>
set -eu
protopy=$1 parent=$2

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# 1. Installed layout: <prefix>/bin/protopy and <prefix>/<parent>/python3.14. The
#    stdlib holds only a probe module, which the source tree's copy does not have,
#    so the import succeeds only if the installed directory was chosen. The copied
#    binary keeps its absolute build RPATH, so its libraries still resolve.
mkdir -p "$tmp/prefix/bin" "$tmp/prefix/$parent/python3.14"
cp "$protopy" "$tmp/prefix/bin/protopy"
echo 'OK = 1' > "$tmp/prefix/$parent/python3.14/_pp_stdlib_layout_probe.py"
(cd / && "$tmp/prefix/bin/protopy" -c 'import _pp_stdlib_layout_probe, sys
assert sys.path[0].endswith("python3.14"), sys.path') \
  || { echo "FAIL: installed stdlib layout not found"; exit 1; }

# 2. An empty ../lib/python3.14 relative to the working directory must not hijack
#    the stdlib of the build-tree binary.
mkdir -p "$tmp/trap/lib/python3.14" "$tmp/trap/cwd"
(cd "$tmp/trap/cwd" && "$protopy" -c 'import textwrap') \
  || { echo "FAIL: a lib/python3.14 near the working directory replaced the stdlib"; exit 1; }

# 3. The marker value "1" is not a search path.
(cd / && env -u PROTO_PYTHONPATH "$protopy" -c 'import sys
assert "1" not in sys.path, sys.path') \
  || { echo "FAIL: PROTO_PYTHONPATH marker used as a search path"; exit 1; }

echo "stdlib layout OK"
