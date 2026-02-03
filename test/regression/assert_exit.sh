#!/usr/bin/env bash
expected="$1"
shift

"$@"
status=$?

if [ "$status" -ne "$expected" ]; then
  echo "Expected exit code $expected but got $status"
  exit 1
fi

exit 0
