#!/bin/bash
PROTO_PYTHONPATH=/mnt/c/Users/gamar/PycharmProjects/protoPython/lib/python3.14
export PROTO_PYTHONPATH
gdb -batch -ex "run" -ex "bt full" -ex "quit" --args /mnt/c/Users/gamar/PycharmProjects/protoPython/wsl-build/src/runtime/protopy lib/python3.14/test/test_grammar.py > gdb_out.txt 2>&1 &
GDB_PID=$!
sleep 15
kill -INT $GDB_PID
wait $GDB_PID
