#!/bin/bash
gdb -batch -ex run -ex "thread apply all bt full" -ex quit --args ./cmake-build-asan/src/runtime/protopy "$@" > gdb_trace.txt 2>&1 &
GDB_PID=$!
sleep 20
PROTOPY_PID=$(pgrep -x protopy)
if [ -z "$PROTOPY_PID" ]; then
    PROTOPY_PID=$(pgrep -f "protopy $@" | grep -v gdb)
fi
if [ -n "$PROTOPY_PID" ]; then
    kill -SEGV $PROTOPY_PID
fi
wait $GDB_PID
