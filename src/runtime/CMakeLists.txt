# protopy: minimal runtime executable (stubbed execution; bytecode executor TBD)
# Override default stdlib path when building for install (e.g. -DSTDLIB_INSTALL_PATH=/usr/share/protoPython/python3.14)
if(DEFINED STDLIB_INSTALL_PATH)
    set(_stdlib_path "${STDLIB_INSTALL_PATH}")
else()
    # Relative path from CMAKE_INSTALL_BINDIR to lib/python3.14
    set(_stdlib_path "../${CMAKE_INSTALL_LIBDIR}/python3.14")
endif()

# For local development in build tree, we might want a different default
# but for packageability, relative is better.

add_executable(protopy main.cpp)
target_link_libraries(protopy PRIVATE protoPython)
target_compile_definitions(protopy PRIVATE
    STDLIB_PATH="${_stdlib_path}"
)
# Allow default stdlib path to be overridden by caller if needed
target_include_directories(protopy PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
