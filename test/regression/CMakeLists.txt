# GoogleTest-based regression suite. gtest_main comes from protoCore's
# FetchContent call and exists only when protoCore is a subdirectory of this
# build; see test/library/CMakeLists.txt for why the guard is needed.
if(NOT TARGET gtest_main)
    message(STATUS
        "protoPython: gtest_main is not available -- test/regression is not built")
    return()
endif()

add_executable(test_regr test_regr.cpp)
target_link_libraries(test_regr PRIVATE protoPython gtest_main)
target_compile_definitions(test_regr PRIVATE
    STDLIB_PATH="${CMAKE_SOURCE_DIR}/lib/python3.14"
    REGR_PATH="${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME test_regr COMMAND test_regr)
set_tests_properties(test_regr PROPERTIES LABELS "regression_gate")
