#!/usr/bin/env bash
# Reports const_cast<...ProtoObject...> uses in module and execution code.
#
# A const_cast on a ProtoObject is the normal idiom for protoCore's
# const-returning API (for example casting the result of
# ctx->newObject(true), or passing a mutable frame by reference), so a match
# is not a defect by itself. The report is a list of sites to review for
# mutation of shared, immutable state; it is not a pass/fail gate.
#
# Usage: check_const_cast.sh [--list] [--strict]
#   (default)  print the number of matches per file and the total; exit 0
#   --list     also print every matching line (file:line: text)
#   --strict   exit 1 when there is at least one match
#
# The script can be run from any directory.

set -eu

LIST=0
STRICT=0
for arg in "$@"; do
  case "$arg" in
    --list) LIST=1 ;;
    --strict) STRICT=1 ;;
    -h|--help)
      sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "check_const_cast.sh: unknown option: $arg" >&2
      exit 2
      ;;
  esac
done

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$ROOT"

PATTERN='const_cast.*ProtoObject'
PATHS="src/library/PythonEnvironment.cpp src/library/ExecutionEngine.cpp src/library/SysModule.cpp"

TOTAL=0
for f in $PATHS; do
  if [ ! -f "$f" ]; then
    echo "check_const_cast.sh: missing file: $f" >&2
    exit 2
  fi
  COUNT=$(grep -c "$PATTERN" "$f" || true)
  TOTAL=$((TOTAL + COUNT))
  printf '%6d  %s\n' "$COUNT" "$f"
  if [ "$LIST" -eq 1 ] && [ "$COUNT" -gt 0 ]; then
    grep -n "$PATTERN" "$f" | sed "s|^|  $f:|"
  fi
done
printf '%6d  total const_cast<...ProtoObject...> sites to review\n' "$TOTAL"

if [ "$STRICT" -eq 1 ] && [ "$TOTAL" -gt 0 ]; then
  echo "const_cast check (--strict): FAIL, $TOTAL site(s) found"
  exit 1
fi
exit 0
