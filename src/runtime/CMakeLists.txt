# protopy: minimal runtime executable (stubbed execution; bytecode executor TBD)
# Override default stdlib path when building for install (e.g. -DSTDLIB_INSTALL_PATH=/usr/share/protoPython/python3.14)
if(DEFINED STDLIB_INSTALL_PATH)
    set(_stdlib_path "${STDLIB_INSTALL_PATH}")
else()
    set(_stdlib_path "${CMAKE_SOURCE_DIR}/lib/python3.14")
endif()
add_executable(protopy main.cpp)
target_link_libraries(protopy PRIVATE protoPython)
target_compile_definitions(protopy PRIVATE
    STDLIB_PATH="${_stdlib_path}"
)
# Allow default stdlib path to be overridden by caller if needed
target_include_directories(protopy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
