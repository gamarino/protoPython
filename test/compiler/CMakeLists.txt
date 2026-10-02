# run_module: minimal dlopen harness used by the benchmark suite to
# load a protopyc-generated .so and invoke proto_module_init.  Lives
# under test/compiler/ because it is a development helper, not a
# distributable; it is built so a normal `cmake --build` keeps it in
# sync with the current libprotoPython / libprotoCore ABI (the manual
# build path that preceded this CMakeLists drifted in May 2026 and
# silently broke the protopyc benchmark column).
#
# The C++ protopyc emits has no `long` and no compiler builtins, and compiles to
# an object file with the compiler that built protoPython -- on every platform,
# MSVC included. Only emitting and compiling is checked here: building and
# loading a module (below) needs make and a shared-library loader that protopyc
# supports on Linux and macOS only.
if(MSVC)
    set(_emitted_msvc 1)
else()
    set(_emitted_msvc 0)
endif()
add_test(NAME protopyc_emitted_cpp_portable
    COMMAND ${CMAKE_COMMAND}
            -DPROTOPYC=$<TARGET_FILE:protopyc>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/portable_ints_probe.py
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/emitted_cpp_portable
            -DCXX=${CMAKE_CXX_COMPILER}
            "-DINCLUDE_DIRS=${PROJECT_SOURCE_DIR}/include;${PROTOPYTHON_PROTOCORE_INCLUDE_DIR}"
            -DMSVC=${_emitted_msvc}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_emitted_cpp.cmake)
set_tests_properties(protopyc_emitted_cpp_portable PROPERTIES
    TIMEOUT 300
    LABELS "regression_gate")

# protopyc builds a module with a C++ compiler and make, through a bash script;
# that path is Linux/macOS only, and so are these targets.
if(WIN32)
    return()
endif()

add_executable(run_module run_module.cpp)

target_include_directories(run_module PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
)

target_link_libraries(run_module PRIVATE
    protoPython
    protoCore
    dl
)

# The Makefile from `protopyc --emit-make` uses this build's include and library
# directories (with an RPATH and PROTOPYC_* overrides) and builds a module that
# protopy imports.
add_test(NAME protopyc_makefile_paths
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/protopyc_makefile_paths.sh
            $<TARGET_FILE:protopyc> $<TARGET_FILE:protopy>
            ${PROJECT_SOURCE_DIR}/include $<TARGET_FILE_DIR:protoPython>)
set_tests_properties(protopyc_makefile_paths PROPERTIES
    TIMEOUT 120
    LABELS "regression_gate")

# A module built by protopyc multiplies 64-bit integers and promotes overflowing
# products to big integers.
add_test(NAME protopyc_portable_ints
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/protopyc_portable_ints.sh
            $<TARGET_FILE:protopyc> $<TARGET_FILE:protopy>
            ${CMAKE_CURRENT_SOURCE_DIR}/portable_ints_probe.py)
set_tests_properties(protopyc_portable_ints PROPERTIES
    TIMEOUT 300
    LABELS "regression_gate")
