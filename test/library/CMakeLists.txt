add_executable(test_foundation TestFoundation.cpp)
target_link_libraries(test_foundation PRIVATE protoPython gtest_main)
target_compile_definitions(test_foundation PRIVATE STDLIB_PATH="${CMAKE_SOURCE_DIR}/lib/python3.14")
add_test(NAME test_foundation COMMAND test_foundation)
set_tests_properties(test_foundation PROPERTIES TIMEOUT 60)

# Minimal reproducer without GTest (for debugging hangs)
add_executable(test_minimal TestMinimal.cpp)
target_link_libraries(test_minimal PRIVATE protoPython)
target_compile_definitions(test_minimal PRIVATE STDLIB_PATH="${CMAKE_SOURCE_DIR}/lib/python3.14")

# ProtoCore-only minimal test (no protoPython)
add_executable(test_protocore_minimal TestProtoCoreMinimal.cpp)
target_link_libraries(test_protocore_minimal PRIVATE protoCore)
