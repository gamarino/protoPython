# CMake generated Testfile for 
# Source directory: /home/gamarino/Documentos/proyectos/protoPython
# Build directory: /home/gamarino/Documentos/proyectos/protoPython/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[regrtest_protopy_script]=] "/home/gamarino/Documentos/proyectos/protoPython/build/src/runtime/protopy" "/home/gamarino/Documentos/proyectos/protoPython/test/regression/regrtest_runner.py")
set_tests_properties([=[regrtest_protopy_script]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/gamarino/Documentos/proyectos/protoPython/CMakeLists.txt;22;add_test;/home/gamarino/Documentos/proyectos/protoPython/CMakeLists.txt;0;")
subdirs("protoCore")
subdirs("src/library")
subdirs("test/library")
subdirs("src/runtime")
subdirs("test/regression")
