#!/usr/bin/env bash
# Phase 1.2 immutability verification: fail if const_cast is used in module/import paths.
# Allowed: only in explicitly documented, isolated places (e.g. internal GC); not for shared state.
# Run from protoPython repo root.

set -e
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$ROOT"

FAIL=0
# Paths that must not use const_cast to mutate shared state (module wrappers, cache, sys.modules)
PATHS="src/library/PythonEnvironment.cpp src/library/ExecutionEngine.cpp src/library/SysModule.cpp"
for f in $PATHS; do
  if [ -f "$f" ]; then
    # const_cast followed by setAttribute or similar mutation on shared objects is the violation
    if grep -n 'const_cast.*ProtoObject' "$f" | grep -q .; then
      echo "CHECK: $f contains const_cast<...ProtoObject...> (review for shared-state mutation)"
      grep -n 'const_cast.*ProtoObject' "$f" || true
      FAIL=1
    fi
  fi
done

if [ $FAIL -eq 1 ]; then
  echo "Immutability check: FAIL (const_cast in module/execution paths; see TEST_PLAN.md Assertion of Immutability)"
  exit 1
fi
echo "Immutability check: PASS (no const_cast on ProtoObject in listed paths, or none found)"
exit 0
