# protopy: minimal runtime executable (stubbed execution; bytecode executor TBD)
add_executable(protopy main.cpp)
target_link_libraries(protopy PRIVATE protoPython)
target_compile_definitions(protopy PRIVATE
    STDLIB_PATH="${CMAKE_SOURCE_DIR}/lib/python3.14"
)
# Allow default stdlib path to be overridden by caller if needed
target_include_directories(protopy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
