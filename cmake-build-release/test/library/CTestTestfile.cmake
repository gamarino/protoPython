# CMake generated Testfile for 
# Source directory: /mnt/c/Users/gamar/PycharmProjects/protoPython/test/library
# Build directory: /mnt/c/Users/gamar/PycharmProjects/protoPython/cmake-build-release/test/library
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[test_foundation]=] "/mnt/c/Users/gamar/PycharmProjects/protoPython/cmake-build-release/test/library/test_foundation" "--gtest_filter=\"FoundationTest.BasicTypesExist:FoundationTest.ResolveBuiltins:FoundationTest.ModuleImport:FoundationTest.BuiltinsModule:FoundationTest.SysModule:FoundationTest.ExecuteModule:FoundationTest.IOModule:FoundationTest.FilterBuiltin\"")
set_tests_properties([=[test_foundation]=] PROPERTIES  TIMEOUT "30" _BACKTRACE_TRIPLES "/mnt/c/Users/gamar/PycharmProjects/protoPython/test/library/CMakeLists.txt;5;add_test;/mnt/c/Users/gamar/PycharmProjects/protoPython/test/library/CMakeLists.txt;0;")
add_test([=[test_execution_engine]=] "/mnt/c/Users/gamar/PycharmProjects/protoPython/cmake-build-release/test/library/test_execution_engine")
set_tests_properties([=[test_execution_engine]=] PROPERTIES  _BACKTRACE_TRIPLES "/mnt/c/Users/gamar/PycharmProjects/protoPython/test/library/CMakeLists.txt;18;add_test;/mnt/c/Users/gamar/PycharmProjects/protoPython/test/library/CMakeLists.txt;0;")
add_test([=[test_threading_strategy]=] "/mnt/c/Users/gamar/PycharmProjects/protoPython/cmake-build-release/test/library/test_threading_strategy")
set_tests_properties([=[test_threading_strategy]=] PROPERTIES  _BACKTRACE_TRIPLES "/mnt/c/Users/gamar/PycharmProjects/protoPython/test/library/CMakeLists.txt;23;add_test;/mnt/c/Users/gamar/PycharmProjects/protoPython/test/library/CMakeLists.txt;0;")
add_test([=[test_basic_block_analysis]=] "/mnt/c/Users/gamar/PycharmProjects/protoPython/cmake-build-release/test/library/test_basic_block_analysis")
set_tests_properties([=[test_basic_block_analysis]=] PROPERTIES  _BACKTRACE_TRIPLES "/mnt/c/Users/gamar/PycharmProjects/protoPython/test/library/CMakeLists.txt;28;add_test;/mnt/c/Users/gamar/PycharmProjects/protoPython/test/library/CMakeLists.txt;0;")
add_test([=[test_hpy_context]=] "/mnt/c/Users/gamar/PycharmProjects/protoPython/cmake-build-release/test/library/test_hpy_context")
set_tests_properties([=[test_hpy_context]=] PROPERTIES  _BACKTRACE_TRIPLES "/mnt/c/Users/gamar/PycharmProjects/protoPython/test/library/CMakeLists.txt;33;add_test;/mnt/c/Users/gamar/PycharmProjects/protoPython/test/library/CMakeLists.txt;0;")
