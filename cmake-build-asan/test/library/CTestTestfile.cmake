# CMake generated Testfile for 
# Source directory: /home/gamarino/Documentos/proyectos/protoPython/test/library
# Build directory: /home/gamarino/Documentos/proyectos/protoPython/cmake-build-asan/test/library
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[test_foundation]=] "/home/gamarino/Documentos/proyectos/protoPython/cmake-build-asan/test/library/test_foundation" "--gtest_filter=\"FoundationTest.BasicTypesExist:FoundationTest.ResolveBuiltins:FoundationTest.ModuleImport:FoundationTest.BuiltinsModule:FoundationTest.SysModule:FoundationTest.ExecuteModule:FoundationTest.IOModule\"")
set_tests_properties([=[test_foundation]=] PROPERTIES  TIMEOUT "30" _BACKTRACE_TRIPLES "/home/gamarino/Documentos/proyectos/protoPython/test/library/CMakeLists.txt;5;add_test;/home/gamarino/Documentos/proyectos/protoPython/test/library/CMakeLists.txt;0;")
add_test([=[test_execution_engine]=] "/home/gamarino/Documentos/proyectos/protoPython/cmake-build-asan/test/library/test_execution_engine")
set_tests_properties([=[test_execution_engine]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/gamarino/Documentos/proyectos/protoPython/test/library/CMakeLists.txt;18;add_test;/home/gamarino/Documentos/proyectos/protoPython/test/library/CMakeLists.txt;0;")
add_test([=[test_threading_strategy]=] "/home/gamarino/Documentos/proyectos/protoPython/cmake-build-asan/test/library/test_threading_strategy")
set_tests_properties([=[test_threading_strategy]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/gamarino/Documentos/proyectos/protoPython/test/library/CMakeLists.txt;23;add_test;/home/gamarino/Documentos/proyectos/protoPython/test/library/CMakeLists.txt;0;")
add_test([=[test_basic_block_analysis]=] "/home/gamarino/Documentos/proyectos/protoPython/cmake-build-asan/test/library/test_basic_block_analysis")
set_tests_properties([=[test_basic_block_analysis]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/gamarino/Documentos/proyectos/protoPython/test/library/CMakeLists.txt;28;add_test;/home/gamarino/Documentos/proyectos/protoPython/test/library/CMakeLists.txt;0;")
