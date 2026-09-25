# Tests that need no GoogleTest.

# Minimal reproducer without GTest (for debugging hangs)
add_executable(test_minimal TestMinimal.cpp)
target_link_libraries(test_minimal PRIVATE protoPython)
target_compile_definitions(test_minimal PRIVATE STDLIB_PATH="${CMAKE_SOURCE_DIR}/lib/python3.14")

# ProtoCore-only minimal test (no protoPython)
add_executable(test_protocore_minimal TestProtoCoreMinimal.cpp)
target_link_libraries(test_protocore_minimal PRIVATE protoCore)

# GoogleTest-based tests.
#
# protoPython has no GoogleTest of its own: gtest_main comes from the
# FetchContent call in protoCore's test/ directory, which is only processed when
# protoCore is added as a subdirectory (the developer fallback). With an
# installed protoCore found through find_package the target does not exist, and
# without this guard CMake passed "gtest_main" through to the linker as a bare
# -lgtest_main and the build of the `all` target failed. That made `cpack`
# impossible in exactly the mode a distributable package must be built in,
# because CPack builds `all` through its preinstall target first.
if(NOT TARGET gtest_main)
    message(STATUS
        "protoPython: gtest_main is not available (protoCore was not added as a "
        "subdirectory) -- the GoogleTest-based tests are not built. Configure "
        "without -DPROTOCORE_REQUIRE_PACKAGE=ON, in a tree with ../protoCore, "
        "to build and run the full suite.")
    return()
endif()

add_executable(test_foundation TestFoundation.cpp)
target_link_libraries(test_foundation PRIVATE protoPython gtest_main)
target_compile_definitions(test_foundation PRIVATE STDLIB_PATH="${CMAKE_SOURCE_DIR}/lib/python3.14")
# Run stable foundation tests. Filter: gate subset. Full suite: run test_foundation without filter.
add_test(NAME test_foundation COMMAND test_foundation --gtest_filter="FoundationTest.BasicTypesExist:FoundationTest.ResolveBuiltins:FoundationTest.ModuleImport:FoundationTest.BuiltinsModule:FoundationTest.SysModule:FoundationTest.ExecuteModule:FoundationTest.IOModule:FoundationTest.FilterBuiltin")
set_tests_properties(test_foundation PROPERTIES TIMEOUT 30)

# Execution engine (minimal bytecode)
add_executable(test_execution_engine TestExecutionEngine.cpp)
target_link_libraries(test_execution_engine PRIVATE protoPython protoCore gtest_main)
add_test(NAME test_execution_engine COMMAND test_execution_engine)

# ThreadingStrategy (re-architecture: no mutex, ExecutionTask, runTaskInline, submitTask)
add_executable(test_threading_strategy TestThreadingStrategy.cpp)
target_link_libraries(test_threading_strategy PRIVATE protoPython protoCore gtest_main)
add_test(NAME test_threading_strategy COMMAND test_threading_strategy)

# BasicBlockAnalysis (basic block boundaries for bulk task dispatch)
add_executable(test_basic_block_analysis TestBasicBlockAnalysis.cpp)
target_link_libraries(test_basic_block_analysis PRIVATE protoPython protoCore gtest_main)
add_test(NAME test_basic_block_analysis COMMAND test_basic_block_analysis)

# HPy Phase 1 (context, handle table, core ABI)
add_executable(test_hpy_context TestHPyContext.cpp)
target_link_libraries(test_hpy_context PRIVATE protoPython protoCore gtest_main)
add_test(NAME test_hpy_context COMMAND test_hpy_context)
set_tests_properties(test_hpy_context PROPERTIES TIMEOUT 60 LABELS "regression_gate")

# Compiler unit tests (parse → compile → execute; covers all major language features)
add_executable(test_compiler TestCompiler.cpp)
target_link_libraries(test_compiler PRIVATE protoPython protoCore gtest_main)
target_compile_definitions(test_compiler PRIVATE STDLIB_PATH="${CMAKE_SOURCE_DIR}/lib/python3.14")
add_test(NAME test_compiler COMMAND test_compiler)
set_tests_properties(test_compiler PROPERTIES TIMEOUT 60)

# A finished thread, and a dropped function object, must leave nothing
# reachable.  Measures marked cells, so it gets its own process: sharing one with
# tests that allocate would let another test decide its verdict.
add_executable(test_thread_state_released TestThreadStateReleased.cpp)
target_link_libraries(test_thread_state_released
    PRIVATE protoPython protoCore gtest_main)
target_compile_definitions(test_thread_state_released
    PRIVATE STDLIB_PATH="${CMAKE_SOURCE_DIR}/lib/python3.14")
add_test(NAME test_thread_state_released COMMAND test_thread_state_released)
set_tests_properties(test_thread_state_released
                     PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE
                     LABELS "regression_gate")

# protoCore's embedder conformance suite (P4).
#
# protoPython consumes protoCore by add_subdirectory in developer mode, so the
# conformance library is reached as the in-tree target `protoCoreConformance`;
# in package mode it is `protoCore::conformance`.  Both are optional, so a
# protoCore older than 2.3 simply does not build these targets.
if(TARGET protoCoreConformance)
    set(_protopy_conformance protoCoreConformance)
elseif(TARGET protoCore::conformance)
    set(_protopy_conformance protoCore::conformance)
endif()

if(_protopy_conformance)
    # Its own executable: the GC cases measure heap state, so sharing a process
    # with tests that allocate would let one test decide another's verdict.
    add_executable(test_conformance ConformanceTests.cpp)
    target_link_libraries(test_conformance
        PRIVATE protoPython protoCore ${_protopy_conformance} gtest_main)
    target_compile_definitions(test_conformance
        PRIVATE STDLIB_PATH="${CMAKE_SOURCE_DIR}/lib/python3.14")
    add_test(NAME test_conformance COMMAND test_conformance)
    set_tests_properties(test_conformance PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE)

    # The three cases that cannot report their own failure in-process: one
    # aborts, two deadlock the space.  The external TIMEOUT is the verdict.
    add_executable(test_conformance_isolate ConformanceIsolate.cpp)
    target_link_libraries(test_conformance_isolate
        PRIVATE protoPython protoCore ${_protopy_conformance})
    target_compile_definitions(test_conformance_isolate
        PRIVATE STDLIB_PATH="${CMAKE_SOURCE_DIR}/lib/python3.14")
    foreach(_case heap.ceiling_progress join.parks stw.quorum_completes)
        add_test(NAME conformance_isolate.${_case}
                 COMMAND test_conformance_isolate --case=${_case})
        set_tests_properties(conformance_isolate.${_case}
                             PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    endforeach()
else()
    message(STATUS "protoPython: protoCoreConformance not available; the "
                   "embedder conformance suite is not built.")
endif()
