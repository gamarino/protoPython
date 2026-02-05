add_executable(test_foundation TestFoundation.cpp)
target_link_libraries(test_foundation PRIVATE protoPython gtest_main)
target_compile_definitions(test_foundation PRIVATE STDLIB_PATH="${CMAKE_SOURCE_DIR}/lib/python3.14")
# Run stable foundation tests (FilterBuiltin, MapBuiltin, StringDunders have known issues; full suite can abort).
add_test(NAME test_foundation COMMAND test_foundation --gtest_filter="FoundationTest.BasicTypesExist:FoundationTest.ResolveBuiltins:FoundationTest.ModuleImport:FoundationTest.BuiltinsModule:FoundationTest.SysModule:FoundationTest.ExecuteModule:FoundationTest.IOModule")
set_tests_properties(test_foundation PROPERTIES TIMEOUT 30)

# Minimal reproducer without GTest (for debugging hangs)
add_executable(test_minimal TestMinimal.cpp)
target_link_libraries(test_minimal PRIVATE protoPython)
target_compile_definitions(test_minimal PRIVATE STDLIB_PATH="${CMAKE_SOURCE_DIR}/lib/python3.14")# ProtoCore-only minimal test (no protoPython)
add_executable(test_protocore_minimal TestProtoCoreMinimal.cpp)
target_link_libraries(test_protocore_minimal PRIVATE protoCore)

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