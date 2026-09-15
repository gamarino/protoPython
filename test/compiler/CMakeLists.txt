# run_module: minimal dlopen harness used by the benchmark suite to
# load a protopyc-generated .so and invoke proto_module_init.  Lives
# under test/compiler/ because it is a development helper, not a
# distributable; it is built so a normal `cmake --build` keeps it in
# sync with the current libprotoPython / libprotoCore ABI (the manual
# build path that preceded this CMakeLists drifted in May 2026 and
# silently broke the protopyc benchmark column).
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
