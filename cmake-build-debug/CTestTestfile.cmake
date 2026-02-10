# CMake generated Testfile for 
# Source directory: /home/gamarino/Documentos/proyectos/protoPython
# Build directory: /home/gamarino/Documentos/proyectos/protoPython/cmake-build-debug
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[regrtest_protopy_script]=] "/home/gamarino/Documentos/proyectos/protoPython/cmake-build-debug/src/runtime/protopy" "--script" "/home/gamarino/Documentos/proyectos/protoPython/test/regression/regrtest_runner.py")
set_tests_properties([=[regrtest_protopy_script]=] PROPERTIES  LABELS "regression_gate" _BACKTRACE_TRIPLES "/home/gamarino/Documentos/proyectos/protoPython/CMakeLists.txt;22;add_test;/home/gamarino/Documentos/proyectos/protoPython/CMakeLists.txt;0;")
add_test([=[regrtest_persistence]=] "/usr/local/bin/python3" "/home/gamarino/Documentos/proyectos/protoPython/test/regression/run_and_validate_output.py")
set_tests_properties([=[regrtest_persistence]=] PROPERTIES  ENVIRONMENT "PROTOPY_BIN=/home/gamarino/Documentos/proyectos/protoPython/cmake-build-debug/src/runtime/protopy" LABELS "regression_gate" WORKING_DIRECTORY "/home/gamarino/Documentos/proyectos/protoPython/test/regression" _BACKTRACE_TRIPLES "/home/gamarino/Documentos/proyectos/protoPython/CMakeLists.txt;28;add_test;/home/gamarino/Documentos/proyectos/protoPython/CMakeLists.txt;0;")
add_test([=[protopy_cli_help]=] "/home/gamarino/Documentos/proyectos/protoPython/cmake-build-debug/src/runtime/protopy" "--help")
set_tests_properties([=[protopy_cli_help]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/gamarino/Documentos/proyectos/protoPython/CMakeLists.txt;36;add_test;/home/gamarino/Documentos/proyectos/protoPython/CMakeLists.txt;0;")
add_test([=[protopy_cli_missing_module]=] "/home/gamarino/Documentos/proyectos/protoPython/test/regression/assert_exit.sh" "65" "/home/gamarino/Documentos/proyectos/protoPython/cmake-build-debug/src/runtime/protopy" "--dry-run" "--module" "__proto_missing__")
set_tests_properties([=[protopy_cli_missing_module]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/gamarino/Documentos/proyectos/protoPython/CMakeLists.txt;37;add_test;/home/gamarino/Documentos/proyectos/protoPython/CMakeLists.txt;0;")
add_test([=[protopy_cli_script_success]=] "/home/gamarino/Documentos/proyectos/protoPython/test/regression/assert_exit.sh" "0" "/home/gamarino/Documentos/proyectos/protoPython/cmake-build-debug/src/runtime/protopy" "--dry-run" "--script" "/home/gamarino/Documentos/proyectos/protoPython/test/regression/regrtest_runner.py")
set_tests_properties([=[protopy_cli_script_success]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/gamarino/Documentos/proyectos/protoPython/CMakeLists.txt;39;add_test;/home/gamarino/Documentos/proyectos/protoPython/CMakeLists.txt;0;")
add_test([=[protopy_cli_bytecode_only]=] "/home/gamarino/Documentos/proyectos/protoPython/test/regression/assert_exit.sh" "0" "/home/gamarino/Documentos/proyectos/protoPython/cmake-build-debug/src/runtime/protopy" "--bytecode-only" "--script" "/home/gamarino/Documentos/proyectos/protoPython/test/regression/regrtest_runner.py")
set_tests_properties([=[protopy_cli_bytecode_only]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/gamarino/Documentos/proyectos/protoPython/CMakeLists.txt;41;add_test;/home/gamarino/Documentos/proyectos/protoPython/CMakeLists.txt;0;")
subdirs("protoCore")
subdirs("src/library")
subdirs("test/library")
subdirs("src/runtime")
subdirs("test/regression")
