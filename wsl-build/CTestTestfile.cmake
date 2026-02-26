# CMake generated Testfile for 
# Source directory: /mnt/c/Users/gamar/PycharmProjects/protoPython
# Build directory: /mnt/c/Users/gamar/PycharmProjects/protoPython/wsl-build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[regrtest_protopy_script]=] "/mnt/c/Users/gamar/PycharmProjects/protoPython/wsl-build/src/runtime/protopy" "--script" "/mnt/c/Users/gamar/PycharmProjects/protoPython/test/regression/regrtest_runner.py")
set_tests_properties([=[regrtest_protopy_script]=] PROPERTIES  LABELS "regression_gate" _BACKTRACE_TRIPLES "/mnt/c/Users/gamar/PycharmProjects/protoPython/CMakeLists.txt;47;add_test;/mnt/c/Users/gamar/PycharmProjects/protoPython/CMakeLists.txt;0;")
add_test([=[regrtest_persistence]=] "/usr/bin/python3" "/mnt/c/Users/gamar/PycharmProjects/protoPython/test/regression/run_and_validate_output.py")
set_tests_properties([=[regrtest_persistence]=] PROPERTIES  ENVIRONMENT "PROTOPY_BIN=/mnt/c/Users/gamar/PycharmProjects/protoPython/wsl-build/src/runtime/protopy" LABELS "regression_gate" WORKING_DIRECTORY "/mnt/c/Users/gamar/PycharmProjects/protoPython/test/regression" _BACKTRACE_TRIPLES "/mnt/c/Users/gamar/PycharmProjects/protoPython/CMakeLists.txt;53;add_test;/mnt/c/Users/gamar/PycharmProjects/protoPython/CMakeLists.txt;0;")
add_test([=[protopy_cli_help]=] "/mnt/c/Users/gamar/PycharmProjects/protoPython/wsl-build/src/runtime/protopy" "--help")
set_tests_properties([=[protopy_cli_help]=] PROPERTIES  _BACKTRACE_TRIPLES "/mnt/c/Users/gamar/PycharmProjects/protoPython/CMakeLists.txt;61;add_test;/mnt/c/Users/gamar/PycharmProjects/protoPython/CMakeLists.txt;0;")
add_test([=[protopy_cli_missing_module]=] "/mnt/c/Users/gamar/PycharmProjects/protoPython/test/regression/assert_exit.sh" "65" "/mnt/c/Users/gamar/PycharmProjects/protoPython/wsl-build/src/runtime/protopy" "--dry-run" "--module" "__proto_missing__")
set_tests_properties([=[protopy_cli_missing_module]=] PROPERTIES  _BACKTRACE_TRIPLES "/mnt/c/Users/gamar/PycharmProjects/protoPython/CMakeLists.txt;62;add_test;/mnt/c/Users/gamar/PycharmProjects/protoPython/CMakeLists.txt;0;")
add_test([=[protopy_cli_script_success]=] "/mnt/c/Users/gamar/PycharmProjects/protoPython/test/regression/assert_exit.sh" "0" "/mnt/c/Users/gamar/PycharmProjects/protoPython/wsl-build/src/runtime/protopy" "--dry-run" "--script" "/mnt/c/Users/gamar/PycharmProjects/protoPython/test/regression/regrtest_runner.py")
set_tests_properties([=[protopy_cli_script_success]=] PROPERTIES  _BACKTRACE_TRIPLES "/mnt/c/Users/gamar/PycharmProjects/protoPython/CMakeLists.txt;64;add_test;/mnt/c/Users/gamar/PycharmProjects/protoPython/CMakeLists.txt;0;")
add_test([=[protopy_cli_bytecode_only]=] "/mnt/c/Users/gamar/PycharmProjects/protoPython/test/regression/assert_exit.sh" "0" "/mnt/c/Users/gamar/PycharmProjects/protoPython/wsl-build/src/runtime/protopy" "--bytecode-only" "--script" "/mnt/c/Users/gamar/PycharmProjects/protoPython/test/regression/regrtest_runner.py")
set_tests_properties([=[protopy_cli_bytecode_only]=] PROPERTIES  _BACKTRACE_TRIPLES "/mnt/c/Users/gamar/PycharmProjects/protoPython/CMakeLists.txt;66;add_test;/mnt/c/Users/gamar/PycharmProjects/protoPython/CMakeLists.txt;0;")
subdirs("protoCore")
subdirs("src/library")
subdirs("test/library")
subdirs("src/runtime")
subdirs("test/regression")
subdirs("src/compiler")
