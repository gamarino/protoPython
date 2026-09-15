# protopy: the interpreter executable.
#
# STDLIB_PATH is the installed standard library. By default it is the path from
# the install bin directory to <prefix>/${PROTOPYTHON_STDLIB_INSTALL_PARENT}/python3.14
# (for example ../lib/protoPython/python3.14), which protopy resolves against its own
# directory, so a relocated prefix keeps working. Set STDLIB_INSTALL_PATH to compile
# in another location (absolute, or relative to the executable's directory).
# STDLIB_BUILD_PATH is the source tree's copy, used when STDLIB_PATH does not exist
# (running from the build tree).
if(DEFINED STDLIB_INSTALL_PATH)
    set(_stdlib_path "${STDLIB_INSTALL_PATH}")
else()
    file(RELATIVE_PATH _stdlib_path "${CMAKE_INSTALL_FULL_BINDIR}"
         "${CMAKE_INSTALL_PREFIX}/${PROTOPYTHON_STDLIB_INSTALL_PARENT}/python3.14")
endif()

add_executable(protopy main.cpp)
target_link_libraries(protopy PRIVATE protoPython)
target_compile_definitions(protopy PRIVATE
    STDLIB_PATH="${_stdlib_path}"
    STDLIB_BUILD_PATH="${PROJECT_SOURCE_DIR}/lib/python3.14"
)
target_include_directories(protopy PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    ${PROTOPYTHON_GENERATED_INCLUDE_DIR}
)
