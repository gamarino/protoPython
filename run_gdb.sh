#!/bin/bash
gdb -batch -ex "run" -ex "bt" -ex "frame 1" -ex "print i" -ex "print op" --args ./cmake-build-debug/src/runtime/protopy test_match.py &
GDB_PID=$!
sleep 1
kill -INT $GDB_PID
wait $GDB_PID
