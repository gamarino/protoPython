#!/bin/bash
PID=$(pidof protopy | awk '{print $1}')
if [ -n "$PID" ]; then
    echo "Running gdb on $PID"
    gdb ./cmake-build-release/src/runtime/protopy -p $PID -batch -x gdb_check_threads.txt > gdb_thread_output.txt
else
    echo "protopy not running" > gdb_thread_output.txt
fi
