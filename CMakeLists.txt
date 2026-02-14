cmake_minimum_required(VERSION 3.20)
project(protoPython VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

# --- protoCore: use installed package or build from sibling directory ---
# For packaging: set PROTO_CORE_PREFIX to the install prefix of protoCore (e.g. /usr/local or $HOME/.local).
# For development: leave unset to build protoCore from ../protoCore.
if(DEFINED PROTO_CORE_PREFIX)
    find_library(PROTO_CORE_LIBRARY protoCore
        HINTS "${PROTO_CORE_PREFIX}" PATH_SUFFIXES lib lib64)
    find_path(PROTO_CORE_INCLUDE_DIR NAMES protoCore.h
        HINTS "${PROTO_CORE_PREFIX}" PATH_SUFFIXES include)
    if(PROTO_CORE_LIBRARY AND PROTO_CORE_INCLUDE_DIR)
        add_library(protoCore SHARED IMPORTED GLOBAL)
        set_target_properties(protoCore PROPERTIES
            IMPORTED_LOCATION "${PROTO_CORE_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${PROTO_CORE_INCLUDE_DIR}")
        message(STATUS "Using installed protoCore: ${PROTO_CORE_LIBRARY}")
    else()
        message(FATAL_ERROR "PROTO_CORE_PREFIX=${PROTO_CORE_PREFIX} set but protoCore not found. "
            "Need libprotoCore and include/protoCore.h under that prefix.")
    endif()
else()
    if(NOT TARGET protoCore)
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../protoCore ${CMAKE_CURRENT_BINARY_DIR}/protoCore)
    endif()
    # Build-tree RPATH so executables and libs find each other without LD_LIBRARY_PATH/DYLD_LIBRARY_PATH
    set(CMAKE_BUILD_RPATH "${CMAKE_BINARY_DIR}/src/library" "${CMAKE_BINARY_DIR}/protoCore")
    set(CMAKE_BUILD_RPATH_USE_LINK_PATH TRUE)
endif()

include_directories(include)

# Subdirectories
add_subdirectory(src/library)

enable_testing()
add_subdirectory(test/library)
add_subdirectory(src/runtime)
add_subdirectory(test/regression)
# Test: protopy can resolve and "run" (stubbed) the regression runner script
add_test(NAME regrtest_protopy_script COMMAND $<TARGET_FILE:protopy> --script ${CMAKE_SOURCE_DIR}/test/regression/regrtest_runner.py)
set_tests_properties(regrtest_protopy_script PROPERTIES LABELS "regression_gate")

# Test: run_and_report.py --output persistence (JSON written and valid)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    add_test(NAME regrtest_persistence
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/regression/run_and_validate_output.py)
    set_tests_properties(regrtest_persistence PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/test/regression
        ENVIRONMENT "PROTOPY_BIN=$<TARGET_FILE:protopy>"
        LABELS "regression_gate")
endif()

add_test(NAME protopy_cli_help COMMAND $<TARGET_FILE:protopy> --help)
add_test(NAME protopy_cli_missing_module
    COMMAND ${CMAKE_SOURCE_DIR}/test/regression/assert_exit.sh 65 $<TARGET_FILE:protopy> --dry-run --module __proto_missing__)
add_test(NAME protopy_cli_script_success
    COMMAND ${CMAKE_SOURCE_DIR}/test/regression/assert_exit.sh 0 $<TARGET_FILE:protopy> --dry-run --script ${CMAKE_SOURCE_DIR}/test/regression/regrtest_runner.py)
add_test(NAME protopy_cli_bytecode_only
    COMMAND ${CMAKE_SOURCE_DIR}/test/regression/assert_exit.sh 0 $<TARGET_FILE:protopy> --bytecode-only --script ${CMAKE_SOURCE_DIR}/test/regression/regrtest_runner.py)

# Optional benchmark test (off by default: -DPROTOPY_BENCHMARKS=ON)
option(PROTOPY_BENCHMARKS "Run benchmark suite in CTest" OFF)
if(PROTOPY_BENCHMARKS)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND)
        add_test(NAME protopy_benchmarks
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/benchmarks/run_benchmarks.py --output reports/benchmark_report.md)
        set_tests_properties(protopy_benchmarks PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            ENVIRONMENT "PROTOPY_BIN=$<TARGET_FILE:protopy>"
            LABELS "benchmark")
    endif()
endif()

add_subdirectory(src/compiler)

# --- Install RPATH: installed binaries and libs find each other without LD_LIBRARY_PATH ---
if(UNIX)
    if(APPLE)
        set(_rpath_exe "@executable_path/../${CMAKE_INSTALL_LIBDIR}")
        set(_rpath_lib "@loader_path")
    else()
        set(_rpath_exe "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
        set(_rpath_lib "\$ORIGIN")
    endif()
    set_target_properties(protopy protopyc PROPERTIES INSTALL_RPATH "${_rpath_exe}")
    set_target_properties(protoPython PROPERTIES INSTALL_RPATH "${_rpath_lib}")
endif()

# --- Install rules ---
set(CMAKE_INSTALL_COMPONENT "protoPython")

install(TARGETS protopy protoPython protopyc
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT protoPython
)

install(DIRECTORY include/protoPython
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT protoPython
)

install(DIRECTORY lib/python3.14
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/protoPython
    USE_SOURCE_PERMISSIONS
    COMPONENT protoPython
)

# --- CPack Configuration ---
set(CPACK_PACKAGE_NAME "protopython")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "protoPython - GIL-less Python 3.14 compatible runtime")
set(CPACK_PACKAGE_VENDOR "protoPython")
set(CPACK_PACKAGE_CONTACT "gamarino@gmail.com")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
# set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE") # Ensure this file exists
set(CPACK_COMPONENTS_ALL protoPython)

# Platform-specific configurations
if(UNIX)
    if(APPLE)
        set(CPACK_GENERATOR "DragNDrop")
    else()
        set(CPACK_GENERATOR "DEB;RPM;TGZ")
        set(CPACK_DEBIAN_PACKAGE_MAINTAINER "gamarino@gmail.com")
        set(CPACK_DEBIAN_PACKAGE_SECTION "interpreters")
        set(CPACK_RPM_PACKAGE_LICENSE "MIT")
        set(CPACK_RPM_PACKAGE_GROUP "Development/Languages")
    endif()
elseif(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_HELP_LINK "https://github.com/gamarino/protoPython")
endif()

include(CPack)
