gdb -batch -ex "run test_enum.py" -ex "thread apply all bt" -ex "quit" ./cmake-build-debug/src/runtime/protopy > bt.txt 2>&1 &
PID=$!
sleep 2
kill -INT $PID
wait $PID
