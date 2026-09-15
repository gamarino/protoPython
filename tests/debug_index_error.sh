gdb -batch -ex "break PythonEnvironment.cpp:677" \
           -ex "run" \
           -ex "bt" \
           --args ./build_release/src/runtime/protopy test_import_functools.py > gdb_index_error.txt 2>&1
