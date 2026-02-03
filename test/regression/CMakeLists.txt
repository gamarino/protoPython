add_executable(test_regr test_regr.cpp)
target_link_libraries(test_regr PRIVATE protoPython gtest_main)
target_compile_definitions(test_regr PRIVATE 
    STDLIB_PATH="${CMAKE_SOURCE_DIR}/lib/python3.14"
    REGR_PATH="${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME test_regr COMMAND test_regr)
set_tests_properties(test_regr PROPERTIES LABELS "regression_gate")
