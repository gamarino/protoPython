add_executable(test_foundation TestFoundation.cpp)
target_link_libraries(test_foundation PRIVATE protoPython gtest_main)
target_compile_definitions(test_foundation PRIVATE STDLIB_PATH="${CMAKE_SOURCE_DIR}/lib/python3.14")
add_test(NAME test_foundation COMMAND test_foundation)
