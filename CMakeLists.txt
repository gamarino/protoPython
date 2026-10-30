cmake_minimum_required(VERSION 3.20)
project(protoPython VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler and platform differences, gathered here so the targets below read the
# same on every platform. Outside MSVC these are the flags the build always used.
if(MSVC)
    # Sources are UTF-8; PythonEnvironment.cpp and BuiltinsModule.cpp exceed the
    # default section limit. MSVC never deletes null-pointer checks the way GCC's
    # -fdelete-null-pointer-checks does, so that flag has no counterpart.
    add_compile_options(/utf-8 /bigobj)
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
    # Debug information for the library in every configuration, as -g elsewhere.
    set(PROTOPYTHON_DEBUG_INFO_FLAG "")
else()
    add_compile_options(-fno-delete-null-pointer-checks)
    set(PROTOPYTHON_DEBUG_INFO_FLAG -g)
endif()
# Abort when a ProtoContext is chained onto a context that is not live on the
# calling thread, or destroyed while it is not the innermost one
# (include/protoPython/MemoryManager.hpp). On in Debug builds; the walk it does
# per call is too slow for release builds.
option(PROTOPY_CHECK_CONTEXT_CHAIN
    "Check every ProtoContext protoPython creates against the thread's context chain" OFF)
if(PROTOPY_CHECK_CONTEXT_CHAIN)
    add_compile_definitions(PROTOPY_CHECK_CONTEXT_CHAIN)
else()
    add_compile_definitions($<$<CONFIG:Debug>:PROTOPY_CHECK_CONTEXT_CHAIN>)
endif()
if(WIN32)
    # Windows has no rpath: a program finds its DLLs next to itself, so every
    # executable and DLL of the build goes to one directory.
    if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    endif()
endif()
# Warning level of protoPython's own targets (the library, protopy, protopyc):
# MSVC's /W3, which reports narrowing conversions (C4244, C4267) among others.
# CMake no longer adds /W3 by itself (policy CMP0092), and MSVC's default is
# /W1. Other compilers keep their defaults.
function(protopython_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W3)
    endif()
endfunction()
if(MSVC)
    set(PROTOPYTHON_WINDOWS_STACK_RESERVE "33554432" CACHE STRING
        "Stack reserve, in bytes, of protoPython's executables on Windows")
endif()
# Windows: a UTF-8 process code page for an executable (argv, getenv and paths
# are UTF-8, as elsewhere), and the stack reserve below. Does nothing on other
# platforms.
function(protopython_windows_executable target)
    if(WIN32)
        target_sources(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src/windows/utf8.manifest")
    endif()
    if(MSVC)
        # The interpreter recurses on the native stack, one dispatcher frame per
        # Python call. Windows gives a thread 1 MiB unless the executable asks
        # for more (Linux: 8 MiB), and that held fewer than 100 Python calls,
        # far below the recursion limit of 1000; MSVC's frames for the
        # dispatcher are also larger than GCC's. The reserve applies to the main
        # thread and to every thread created with the default size
        # (std::thread, so Python threads too); it is address space, committed
        # only as used.
        target_link_options(${target} PRIVATE "/STACK:${PROTOPYTHON_WINDOWS_STACK_RESERVE}")
    endif()
endfunction()

include(GNUInstallDirs)

# Installed standard library: <prefix>/<PROTOPYTHON_STDLIB_INSTALL_PARENT>/python3.14.
# The install rule below and the path compiled into protopy (src/runtime) both use it.
set(PROTOPYTHON_STDLIB_INSTALL_PARENT "${CMAKE_INSTALL_LIBDIR}/protoPython")

# --- protoCore -----------------------------------------------------------------
# Resolution order (Phase I, decision D-I1):
#   1. An installed protoCore CMake package: find_package(protoCore 2.0 CONFIG).
#      This is the only mode that checks protoCore's version and ABI, so it is
#      the only mode a distributable package may be built in. Point at a prefix
#      outside the default search path with -DPROTO_CORE_PREFIX=<prefix> or
#      -DCMAKE_PREFIX_PATH=<prefix>.
#   2. Developer fallback: build protoCore from the sibling source tree
#      ../protoCore with add_subdirectory(). Used only when no installed package
#      was found and no prefix was named; refused by -DPROTOCORE_REQUIRE_PACKAGE=ON.
#
# The version floor is 2.11: attribute write groups publish a run of writes to
# one object with ProtoObject::setAttributes, which protoCore first provides in
# 2.11.0. Before that the floor was 2.7: protoPython spells protoCore's 64-bit integers
# proto::proto_long / proto::proto_ulong (and PROTO_L / PROTO_UL), which first
# exist in protoCore 2.7.0; they are long / unsigned long on Linux and macOS and
# long long on Windows. (Before that the floor was 2.0: no other protoCore API
# newer than 2.0.0 is used.) The ceiling is the next major, because protoCore's
# major version and its soname move together; the SOVERSION is asserted below.
# 3.0.0: methods receive their arguments as ProtoArguments /
# ProtoKeywordArguments descriptors (protoCore docs/CALLING_CONVENTION.md).
set(PROTOCORE_MIN_VERSION      "3.0")
set(PROTOCORE_MIN_VERSION_FULL "3.0.0")
set(PROTOCORE_NEXT_MAJOR       "4.0.0")
set(PROTOCORE_ABI_SOVERSION    "4")
option(PROTOCORE_REQUIRE_PACKAGE
    "Require an installed protoCore CMake package; never fall back to ../protoCore" OFF)

if(DEFINED PROTO_CORE_PREFIX)
    list(APPEND CMAKE_PREFIX_PATH "${PROTO_CORE_PREFIX}")
endif()

find_package(protoCore ${PROTOCORE_MIN_VERSION} QUIET CONFIG)

if(protoCore_FOUND)
    if(NOT protoCore_SOVERSION STREQUAL PROTOCORE_ABI_SOVERSION)
        message(FATAL_ERROR
            "protoCore package at ${protoCore_DIR} declares SOVERSION "
            "${protoCore_SOVERSION}; protoPython is built against SOVERSION "
            "${PROTOCORE_ABI_SOVERSION}. Install a matching protoCore.")
    endif()
    # This project links the unqualified target name `protoCore` and uses
    # $<TARGET_FILE_DIR:protoCore> (src/library/CMakeLists.txt, src/compiler/
    # CMakeLists.txt, test/library/CMakeLists.txt). Promote the imported target
    # to global scope and alias it, so none of those references has to change.
    if(NOT TARGET protoCore)
        set_target_properties(protoCore::protoCore PROPERTIES IMPORTED_GLOBAL TRUE)
        add_library(protoCore ALIAS protoCore::protoCore)
    endif()
    set(PROTOPYTHON_PROTOCORE_INCLUDE_DIR "${protoCore_INCLUDE_DIR}")
    set(PROTOPYTHON_PROTOCORE_EXTERNAL TRUE)
    if(APPLE)
        # The package's dylib is @rpath/libprotoCore.<N>.dylib: give the
        # build-tree executables an rpath to its directory, or dyld cannot find
        # it (exit 127) when the prefix is not a default search location.
        list(APPEND CMAKE_BUILD_RPATH "${protoCore_LIB_DIR}")
    endif()
    message(STATUS "protoPython: installed protoCore ${protoCore_VERSION} "
                   "(SOVERSION ${protoCore_SOVERSION}) from ${protoCore_DIR}")
elseif(DEFINED PROTO_CORE_PREFIX OR PROTOCORE_REQUIRE_PACKAGE)
    set(_protoCore_where "")
    if(DEFINED PROTO_CORE_PREFIX)
        set(_protoCore_where " under ${PROTO_CORE_PREFIX}")
    endif()
    message(FATAL_ERROR
        "No protoCore >= ${PROTOCORE_MIN_VERSION_FULL} CMake package was found"
        "${_protoCore_where}.\n"
        "  A prefix holding only libprotoCore and protoCore.h is no longer "
        "accepted: without lib/cmake/protoCore/protoCoreConfig.cmake there is "
        "no way to tell protoCore 1.x from 2.x, and linking the wrong one is "
        "silent. Build and install protoCore ${PROTOCORE_MIN_VERSION_FULL} or "
        "later:\n"
        "    cmake -S ../protoCore -B ../protoCore/build_release "
        "-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<prefix>\n"
        "    cmake --build ../protoCore/build_release --target protoCore\n"
        "    cmake --install ../protoCore/build_release --component protoCore")
else()
    set(PROTOCORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../protoCore")
    if(NOT EXISTS "${PROTOCORE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "No installed protoCore package and no sibling source tree at "
            "${PROTOCORE_DIR}. Install protoCore ${PROTOCORE_MIN_VERSION_FULL} "
            "or pass -DPROTO_CORE_PREFIX=<prefix>.")
    endif()
    message(WARNING
        "protoPython: no installed protoCore package found; building protoCore "
        "from ${PROTOCORE_DIR}. This developer mode performs no package version "
        "check and must not be used to produce a distributable package "
        "(configure with -DPROTOCORE_REQUIRE_PACKAGE=ON to forbid it).")
    if(NOT TARGET protoCore)
        add_subdirectory("${PROTOCORE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/protoCore")
    endif()
    # project() sets protoCore_VERSION in the subdirectory's scope, not here;
    # the SOVERSION target property is readable from the parent.
    get_target_property(_protoCore_sibling_soversion protoCore SOVERSION)
    if(NOT _protoCore_sibling_soversion STREQUAL PROTOCORE_ABI_SOVERSION)
        message(FATAL_ERROR
            "The sibling protoCore at ${PROTOCORE_DIR} has SOVERSION "
            "${_protoCore_sibling_soversion}; protoPython is built against "
            "SOVERSION ${PROTOCORE_ABI_SOVERSION}.")
    endif()
    unset(_protoCore_sibling_soversion)
    # The sibling tree's version is not visible here; probe for the newest API
    # the sources use instead (ProtoObject::setAttributes, protoCore 2.11.0).
    file(STRINGS "${PROTOCORE_DIR}/headers/protoCore.h" _protoCore_setAttributes
         REGEX "setAttributes\\(")
    if(NOT _protoCore_setAttributes)
        message(FATAL_ERROR
            "The sibling protoCore at ${PROTOCORE_DIR} predates "
            "ProtoObject::setAttributes (protoCore 2.11.0); protoPython needs "
            "protoCore ${PROTOCORE_MIN_VERSION_FULL} or later.")
    endif()
    unset(_protoCore_setAttributes)
    get_filename_component(PROTOPYTHON_PROTOCORE_INCLUDE_DIR
        "${PROTOCORE_DIR}/headers" ABSOLUTE)
    set(PROTOPYTHON_PROTOCORE_EXTERNAL FALSE)
    # Build-tree RPATH so executables and libs find each other without LD_LIBRARY_PATH/DYLD_LIBRARY_PATH
    set(CMAKE_BUILD_RPATH "${CMAKE_BINARY_DIR}/src/library" "${CMAKE_BINARY_DIR}/protoCore")
    set(CMAKE_BUILD_RPATH_USE_LINK_PATH TRUE)
endif()

include_directories(include)

# Version header generated from project(); included (not installed) by the
# library and protopy for the REPL banner, help(), --help and sys.version.
set(PROTOPYTHON_GENERATED_INCLUDE_DIR "${PROJECT_BINARY_DIR}/generated")
# Build date for sys.version ("Mon DD YYYY", as CPython's), taken when CMake
# configures the build. string(TIMESTAMP) uses SOURCE_DATE_EPOCH when it is
# set, so reproducible builds get a fixed date.
string(TIMESTAMP PROTOPYTHON_BUILD_DATE "%b %d %Y" UTC)
configure_file(src/Version.h.in "${PROTOPYTHON_GENERATED_INCLUDE_DIR}/protoPython/Version.h" @ONLY)

# --- PCRE2: the engine of the `re` module (src/library/ReModule.cpp) --------
# The 8-bit library, 10.34 or later (PCRE2_MATCH_INVALID_UTF), with its JIT
# where the platform has one. Resolution order: the system's library through
# pkg-config (libpcre2-dev on Debian / Ubuntu, pcre2-devel on Fedora / RHEL,
# pcre2 from Homebrew), then an installed PCRE2 CMake package (vcpkg, a
# prefix given with -DCMAKE_PREFIX_PATH), else PCRE2's pinned release built
# here as a static library (the Windows build, and any system without the
# package; -DPROTOPY_BUNDLED_PCRE2=ON forces it). The bundled copy is linked
# into libprotoPython, so it adds no file to any package; its install rules
# are kept out of protoPython's component. Same arrangement as protoClojure.
option(PROTOPY_BUNDLED_PCRE2 "Build PCRE2's pinned release instead of using the system's" OFF)
set(PROTOPY_PCRE2_TARGET "")
if(NOT PROTOPY_BUNDLED_PCRE2)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(PROTOPY_PCRE2 QUIET IMPORTED_TARGET libpcre2-8>=10.34)
        if(PROTOPY_PCRE2_FOUND)
            set(PROTOPY_PCRE2_TARGET PkgConfig::PROTOPY_PCRE2)
            message(STATUS "protoPython: PCRE2 ${PROTOPY_PCRE2_VERSION} from pkg-config")
        endif()
    endif()
    if(NOT PROTOPY_PCRE2_TARGET)
        find_package(PCRE2 10.34 CONFIG QUIET COMPONENTS 8BIT)
        if(TARGET PCRE2::8BIT)
            set(PROTOPY_PCRE2_TARGET PCRE2::8BIT)
            message(STATUS "protoPython: PCRE2 ${PCRE2_VERSION} CMake package from ${PCRE2_DIR}")
        endif()
    endif()
endif()
if(NOT PROTOPY_PCRE2_TARGET)
    include(FetchContent)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    set(PCRE2_BUILD_PCRE2_8 ON CACHE BOOL "" FORCE)
    set(PCRE2_BUILD_PCRE2_16 OFF CACHE BOOL "" FORCE)
    set(PCRE2_BUILD_PCRE2_32 OFF CACHE BOOL "" FORCE)
    set(PCRE2_SUPPORT_JIT ON CACHE BOOL "" FORCE)
    set(PCRE2_BUILD_PCRE2GREP OFF CACHE BOOL "" FORCE)
    set(PCRE2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(PCRE2_SUPPORT_LIBBZ2 OFF CACHE BOOL "" FORCE)
    set(PCRE2_SUPPORT_LIBZ OFF CACHE BOOL "" FORCE)
    set(PCRE2_SUPPORT_LIBEDIT OFF CACHE BOOL "" FORCE)
    set(PCRE2_SUPPORT_LIBREADLINE OFF CACHE BOOL "" FORCE)
    set(PCRE2_STATIC_PIC ON CACHE BOOL "" FORCE)
    set(_protopy_default_component "${CMAKE_INSTALL_DEFAULT_COMPONENT_NAME}")
    set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME pcre2-bundled)
    set(_protopy_pcre2_exclude "")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
        set(_protopy_pcre2_exclude EXCLUDE_FROM_ALL)
    endif()
    FetchContent_Declare(pcre2
        URL https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.46/pcre2-10.46.tar.bz2
        URL_HASH SHA256=15fbc5aba6beee0b17aecb04602ae39432393aba1ebd8e39b7cabf7db883299f
        ${_protopy_pcre2_exclude})
    unset(_protopy_pcre2_exclude)
    FetchContent_MakeAvailable(pcre2)
    set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME "${_protopy_default_component}")
    unset(_protopy_default_component)
    set(PROTOPY_PCRE2_TARGET pcre2-8-static)
    message(STATUS "protoPython: PCRE2 10.46 built from its release (static, JIT)")
endif()

# Subdirectories
add_subdirectory(src/library)

enable_testing()
# The script tests are bash scripts. Windows cannot run a script directly, so
# there they run through the bash of Git for Windows (not the bash.exe in System32,
# which is WSL); elsewhere the prefix is empty and the script is the command.
if(WIN32)
    find_program(PROTOPYTHON_BASH bash
        HINTS "C:/Program Files/Git/bin" "C:/Program Files/Git/usr/bin" NO_DEFAULT_PATH)
    if(NOT PROTOPYTHON_BASH)
        find_program(PROTOPYTHON_BASH bash)
    endif()
    set(PROTOPYTHON_SH "${PROTOPYTHON_BASH}")
else()
    set(PROTOPYTHON_SH "")
endif()
# Exit-status checks run without a shell on Windows (assert_exit.cmake): Git
# Bash's command-line parsing would drop the quotes inside `protopy -c` code.
if(WIN32)
    set(PROTOPYTHON_ASSERT_EXIT ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/test/regression/assert_exit.cmake --)
else()
    set(PROTOPYTHON_ASSERT_EXIT ${CMAKE_SOURCE_DIR}/test/regression/assert_exit.sh)
endif()
# GoogleTest normally comes from protoCore's own test/ directory, which exists
# only in the developer fallback (add_subdirectory). With an installed protoCore
# the GoogleTest-based tests are skipped (test/library/CMakeLists.txt) unless
# this option fetches GoogleTest itself. It is on by default on Windows, where
# protoCore is always an installed package; elsewhere the default keeps the
# build as it was.
if(WIN32)
    set(_protopython_fetch_gtest_default ON)
else()
    set(_protopython_fetch_gtest_default OFF)
endif()
option(PROTOPYTHON_FETCH_GOOGLETEST
    "Fetch GoogleTest when protoCore does not provide gtest_main"
    ${_protopython_fetch_gtest_default})
if(PROTOPYTHON_FETCH_GOOGLETEST AND NOT TARGET gtest_main)
    include(FetchContent)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    # protoPython links the dynamic C runtime (CMake's default); GoogleTest
    # would otherwise pick the static one under MSVC.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()
add_subdirectory(test/library)
add_subdirectory(src/runtime)
add_subdirectory(test/regression)
add_subdirectory(test/compiler)
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
    # Conformity suite (tests/conformity) against the protopy built here, and the
    # runner's own self-test (binary discovery, PROTO_PYTHONPATH handling).
    add_test(NAME conformity_suite
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/conformity/run_conformity.py)
    set_tests_properties(conformity_suite PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        ENVIRONMENT "PROTO_PYTHON=$<TARGET_FILE:protopy>"
        TIMEOUT 400
        LABELS "regression_gate")
    add_test(NAME conformity_runner_selftest
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/conformity/test_run_conformity.py)
    set_tests_properties(conformity_runner_selftest PROPERTIES
        TIMEOUT 60
        LABELS "regression_gate")
    # protoCore's adaptive heap controller is protopy's default memory policy
    # (PythonEnvironment::getProcessSpace). The driver reads protoCore's
    # per-cycle trace (PROTOCORE_HEAP_TRACE=1) from the child's standard error
    # and measures its peak RSS. Python 3 drives the checks because it measures
    # a child's peak RSS on Linux, macOS and Windows alike.
    #
    # On by default: no limit and no switch in the environment; 10000 rounds
    # allocate about 40 M cells, well past the controller's initial 2 M-cell
    # soft limit.
    add_test(NAME adaptive_heap_on_by_default
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/regression/adaptive_heap_check.py
            --expect on --sentinel "adaptive heap garbage done"
            --unset PROTOCORE_ADAPTIVE_HEAP --unset PROTOCORE_HEAP_LIMIT_CELLS
            --unset PROTOCORE_ADAPTIVE_HEAP_START
            -- $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/adaptive_heap_garbage.py 10000)
    # Off with PROTOCORE_ADAPTIVE_HEAP=0: the budget becomes a fixed hard limit.
    add_test(NAME adaptive_heap_off_by_env
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/regression/adaptive_heap_check.py
            --expect off --sentinel "adaptive heap garbage done"
            --set PROTOCORE_ADAPTIVE_HEAP=0 --set PROTOCORE_HEAP_LIMIT_CELLS=1000000
            --unset PROTOCORE_ADAPTIVE_HEAP_START
            -- $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/adaptive_heap_garbage.py 10000)
    # Bounded memory: a 1 M-cell budget (64 MiB of cells) and 20000 rounds of
    # garbage, about 85 M cells (before the controller this run's peak RSS was
    # 5.6 GB, 2026-10-10, Linux); at least 10 cycles, and the peak RSS bounded
    # by the budget plus the interpreter's own footprint (measured: 73 MB).
    # --deadline 120: about seven times the slowest of 119 Windows CI passes
    # (16.5 s); a run past it is a hang, reported with its trace
    # (adaptive_heap_check.py) instead of being killed silently by TIMEOUT.
    add_test(NAME adaptive_heap_bounded_memory
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/regression/adaptive_heap_check.py
            --expect on --sentinel "adaptive heap garbage done"
            --set PROTOCORE_HEAP_LIMIT_CELLS=1000000
            --unset PROTOCORE_ADAPTIVE_HEAP --unset PROTOCORE_ADAPTIVE_HEAP_START
            --min-cycles 10 --max-rss-mb 160 --deadline 120
            -- $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/adaptive_heap_garbage.py 20000)
    set_tests_properties(adaptive_heap_on_by_default adaptive_heap_off_by_env
        adaptive_heap_bounded_memory PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 300
        LABELS "regression_gate")
    # protopy --dap driven as VS Code drives it: breakpoints, stepping, the call
    # stack, variables, evaluate, stopOnEntry, the script's output as `output`
    # events (more than a pipe buffer, non-ASCII), stdin at end of file, and an
    # uncaught exception (docs/debugging.md).
    add_test(NAME protopy_dap_session
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/regression/dap_session.py
            $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression)
    set_tests_properties(protopy_dap_session PROPERTIES
        TIMEOUT 300
        LABELS "regression_gate")
    # The bytecode listings of docs/VM.md are real `protopy --disasm` output:
    # each docs/vm-examples/NN-name.py is compiled and its listing compared
    # with its golden .txt file and with VM.md's copy
    # (docs/vm-examples/disasm_examples.py; --update regenerates them).
    # Every platform: the compiler numbers local slots in source order
    # (Compiler::collectLocalsFromBody), never in a hash container's
    # iteration order, so libstdc++, libc++ and MSVC give the same bytecode.
    add_test(NAME docs_vm_examples
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/docs/vm-examples/disasm_examples.py
            $<TARGET_FILE:protopy> --check)
    set_tests_properties(docs_vm_examples PROPERTIES
        TIMEOUT 300
        LABELS "regression_gate")
endif()
# protopy finds the standard library in the installed layout relative to its own
# directory, is not misled by a ../lib/python3.14 next to the working directory,
# and does not treat the PROTO_PYTHONPATH marker as a directory.
add_test(NAME protopy_stdlib_layout
    COMMAND ${PROTOPYTHON_SH} ${CMAKE_SOURCE_DIR}/test/regression/stdlib_layout.sh
            $<TARGET_FILE:protopy> ${PROTOPYTHON_STDLIB_INSTALL_PARENT})
set_tests_properties(protopy_stdlib_layout PROPERTIES
    TIMEOUT 60
    LABELS "regression_gate")
# sys.version_info, sys.flags, sys.*_info, os.stat_result and time.struct_time
# are struct sequences: tuples of their visible fields with named attributes.
add_test(NAME protopy_struct_sequences
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/struct_sequences.py)
set_tests_properties(protopy_struct_sequences PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# REPL statements run in __main__ (names persist between lines); the banner,
# help(), --help and sys.version report the project version.
add_test(NAME protopy_repl_names_persist
    COMMAND ${PROTOPYTHON_SH} ${CMAKE_SOURCE_DIR}/test/regression/repl_names_persist.sh
            $<TARGET_FILE:protopy> ${PROJECT_VERSION}
            ${CMAKE_BINARY_DIR}/test/regression/repl_home)
set_tests_properties(protopy_repl_names_persist PROPERTIES
    TIMEOUT 60
    LABELS "regression_gate")
# json and heapq work through their pure-Python modules; _json and _heapq do not exist.
add_test(NAME protopy_json_heapq_pure_python
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/json_heapq_pure_python.py)
set_tests_properties(protopy_json_heapq_pure_python PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# The const_cast review report runs and exits 0 (--strict would fail on any match).
add_test(NAME conformity_const_cast_report
    COMMAND ${PROTOPYTHON_SH} ${CMAKE_SOURCE_DIR}/tests/conformity/scripts/check_const_cast.sh)
set_tests_properties(conformity_const_cast_report PROPERTIES
    TIMEOUT 60
    LABELS "regression_gate")

add_test(NAME protopy_cli_help COMMAND $<TARGET_FILE:protopy> --help)
add_test(NAME protopy_cli_missing_module
    COMMAND ${PROTOPYTHON_ASSERT_EXIT} 65 $<TARGET_FILE:protopy> --dry-run --module __proto_missing__)
add_test(NAME protopy_cli_script_success
    COMMAND ${PROTOPYTHON_ASSERT_EXIT} 0 $<TARGET_FILE:protopy> --dry-run --script ${CMAKE_SOURCE_DIR}/test/regression/regrtest_runner.py)
add_test(NAME protopy_cli_bytecode_only
    COMMAND ${PROTOPYTHON_ASSERT_EXIT} 0 $<TARGET_FILE:protopy> --bytecode-only --script ${CMAKE_SOURCE_DIR}/test/regression/regrtest_runner.py)
# LOAD_METHOD's inline cache must bind every call to its own receiver.
add_test(NAME protopy_load_method_pic_receivers
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/load_method_pic_receivers.py)
set_tests_properties(protopy_load_method_pic_receivers PROPERTIES
    TIMEOUT 60
    LABELS "regression_gate")
# threading.Thread.join() must wait for the thread to finish.
add_test(NAME protopy_thread_join_waits
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/thread_join_waits.py)
set_tests_properties(protopy_thread_join_waits PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# `with lock:` on native context managers must run __enter__/__exit__.
add_test(NAME protopy_with_native_context_manager
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/with_native_context_manager.py)
set_tests_properties(protopy_with_native_context_manager PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# Short-lived threads with allocation churn must not crash (GC address reuse).
add_test(NAME protopy_thread_stress_no_crash
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/thread_stress_no_crash.py)
set_tests_properties(protopy_thread_stress_no_crash PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# list mutators called from parallel threads must not lose or duplicate items.
add_test(NAME protopy_list_mutation_threads
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/list_mutation_threads.py)
set_tests_properties(protopy_list_mutation_threads PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# `lst[i] = x` and list.reverse/sort/*= must not drop concurrent appends.
add_test(NAME protopy_list_rewrite_threads
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/list_rewrite_threads.py)
set_tests_properties(protopy_list_rewrite_threads PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# A running function's frame must survive a collection: closures reading and
# writing free variables while their context crosses the young-generation
# submission threshold.  The tiny threshold and heap limit make the window open
# on every call, so it failed on every run before the fix.
add_test(NAME protopy_frame_survives_collection
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/frame_survives_collection.py)
set_tests_properties(protopy_frame_survives_collection PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    ENVIRONMENT "PROTOCORE_GC_CONTEXT_THRESHOLD=200;PROTOCORE_HEAP_LIMIT_CELLS=1500000"
    TIMEOUT 300
    LABELS "regression_gate")
# A call's f_back is its caller's frame, also after the caller called something
# else (the callee's frame used to stay current after it returned).
add_test(NAME protopy_frame_f_back_after_return
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/frame_f_back_after_return.py)
set_tests_properties(protopy_frame_f_back_after_return PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# The code type lists its co_* members.
add_test(NAME protopy_code_type_members
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/code_type_members.py)
set_tests_properties(protopy_code_type_members PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "code_type_members: ok"
    LABELS "regression_gate")
# What native helpers (iteration, exceptions) allocate during a call must be
# collectable while the caller runs; under a 2.5 M-cell fixed limit this ran
# out of memory when they allocated in the module's context.
add_test(NAME protopy_helper_garbage_is_collected
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/helper_garbage_is_collected.py)
set_tests_properties(protopy_helper_garbage_is_collected PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    ENVIRONMENT "PROTOCORE_HEAP_LIMIT_CELLS=2500000;PROTOCORE_ADAPTIVE_HEAP=0"
    TIMEOUT 600
    LABELS "regression_gate")
# set mutators called from parallel threads must not lose or duplicate items.
add_test(NAME protopy_set_mutation_threads
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/set_mutation_threads.py)
set_tests_properties(protopy_set_mutation_threads PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# dict mutators called from parallel threads must keep keys and values in sync.
add_test(NAME protopy_dict_mutation_threads
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/dict_mutation_threads.py)
set_tests_properties(protopy_dict_mutation_threads PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# obj.name = value honours a __setattr__ override and a data descriptor that
# the object's class inherits, not only the ones the class defines itself.
add_test(NAME protopy_store_attr_inherited_hooks
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/store_attr_inherited_hooks.py)
set_tests_properties(protopy_store_attr_inherited_hooks PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# A run of attribute writes is published as one version; the program's
# meaning is the same with the groups on and off.
add_test(NAME protopy_attr_write_groups
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/attr_write_groups.py)
add_test(NAME protopy_attr_write_groups_off
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/attr_write_groups.py)
set_tests_properties(protopy_attr_write_groups protopy_attr_write_groups_off PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
set_tests_properties(protopy_attr_write_groups_off PROPERTIES
    ENVIRONMENT "PROTOPY_ATTR_GROUPS=off")
# dict keys are found through the key hash alone; inserting keys is linear.
add_test(NAME protopy_dict_key_hash
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/dict_key_hash.py)
set_tests_properties(protopy_dict_key_hash PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# LOAD_NAME/STORE_NAME find own bindings bound to None in module and class scopes.
add_test(NAME protopy_name_none_binding
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/name_none_binding.py)
set_tests_properties(protopy_name_none_binding PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# issubclass dispatches to the metaclass __subclasscheck__ (ABCMeta's __subclasshook__ and registry).
add_test(NAME protopy_abc_subclasscheck
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/abc_subclasscheck.py)
set_tests_properties(protopy_abc_subclasscheck PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# object.__subclasshook__ is a classmethod answering NotImplemented; ABCMeta calls it first.
add_test(NAME protopy_object_subclasshook
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/object_subclasshook.py)
set_tests_properties(protopy_object_subclasshook PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "object_subclasshook: ok"
    LABELS "regression_gate")
# _collections_abc._check_methods follows CPython (NotImplemented for a missing or None method).
add_test(NAME protopy_check_methods
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/check_methods.py)
set_tests_properties(protopy_check_methods PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# vars() of a dict-subclass instance is the instance namespace.
add_test(NAME protopy_vars_dict_subclass
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/vars_dict_subclass.py)
set_tests_properties(protopy_vars_dict_subclass PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# functools.cmp_to_key compares through the cmp function (unittest's loader sorts with it).
add_test(NAME protopy_functools_cmp_to_key
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/functools_cmp_to_key.py)
set_tests_properties(protopy_functools_cmp_to_key PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Creating a class with a large namespace is linear.
add_test(NAME protopy_namedtuple_large_size
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/namedtuple_large_size.py)
set_tests_properties(protopy_namedtuple_large_size PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "namedtuple_large_size: ok"
    LABELS "regression_gate")
# vars(o) is o's namespace: deleting, popping and clearing through it remove attributes.
add_test(NAME protopy_vars_namespace_writes
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/vars_namespace_writes.py)
set_tests_properties(protopy_vars_namespace_writes PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# vars(x) raises TypeError for an object without __dict__.
add_test(NAME protopy_vars_without_dict
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/vars_without_dict.py)
set_tests_properties(protopy_vars_without_dict PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# super() proxies serve every idiom with cells that are not bound to the proxy.
add_test(NAME protopy_super_proxy_idioms
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/super_proxy_idioms.py)
set_tests_properties(protopy_super_proxy_idioms PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# unittest.mock imports and works (Mock, MagicMock, patch, spec'd isinstance).
add_test(NAME protopy_unittest_mock_basics
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/unittest_mock_basics.py)
set_tests_properties(protopy_unittest_mock_basics PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "unittest_mock_basics ok"
    LABELS "regression_gate")
# A class namespace may bind `__code__` or a `__class__` property without being taken for a function or a metaclass.
add_test(NAME protopy_class_namespace_code_and_class
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/class_namespace_code_and_class.py)
set_tests_properties(protopy_class_namespace_code_and_class PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "class_namespace_code_and_class ok"
    LABELS "regression_gate")
# An int is not a bool: isinstance(1, bool) is False.
add_test(NAME protopy_isinstance_int_bool
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/isinstance_int_bool.py)
set_tests_properties(protopy_isinstance_int_bool PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "isinstance_int_bool: ok"
    LABELS "regression_gate")
# __name__/__qualname__/__mro__/__bases__/__base__ are class attributes only: instances, None and ints raise AttributeError.
add_test(NAME protopy_type_only_attributes
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/type_only_attributes.py)
set_tests_properties(protopy_type_only_attributes PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "type_only_attributes: ok"
    LABELS "regression_gate")
# Containers find an element by identity before equality (nan in [nan]).
add_test(NAME protopy_nan_containment
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/nan_containment.py)
set_tests_properties(protopy_nan_containment PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# A float subclass instance holding NaN is unequal to itself.
add_test(NAME protopy_float_subclass_nan
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/float_subclass_nan.py)
set_tests_properties(protopy_float_subclass_nan PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Distinct NaN objects are distinct dict and set keys.
add_test(NAME protopy_nan_dict_keys
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/nan_dict_keys.py)
set_tests_properties(protopy_nan_dict_keys PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# A class's own __getattr__ serves its instances; the class falls back to its metaclass's.
add_test(NAME protopy_class_getattr_hook
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/class_getattr_hook.py)
set_tests_properties(protopy_class_getattr_hook PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "class_getattr_hook: ok"
    LABELS "regression_gate")
# A fork child runs Python code and exits.
add_test(NAME protopy_fork_child_runs
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/fork_child_runs.py)
set_tests_properties(protopy_fork_child_runs PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "fork_child_runs: ok"
    LABELS "regression_gate")
# math.isclose: NaN is never close; keyword-only, non-negative tolerances.
add_test(NAME protopy_math_isclose
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/math_isclose.py)
set_tests_properties(protopy_math_isclose PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# function.__defaults__ / __kwdefaults__ are None without defaults, assignable otherwise.
add_test(NAME protopy_function_defaults_none
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/function_defaults_none.py)
set_tests_properties(protopy_function_defaults_none PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "function_defaults_none: ok"
    LABELS "regression_gate")
# json.dumps escapes strings; re matches code points and supports replacement functions.
add_test(NAME protopy_json_dumps_escapes
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/json_dumps_escapes.py)
set_tests_properties(protopy_json_dumps_escapes PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# A type-level __iter__ that is not a function (MagicMock's) is bound as CPython's slot lookup binds it.
add_test(NAME protopy_magicmock_iteration
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/magicmock_iteration.py)
set_tests_properties(protopy_magicmock_iteration PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "magicmock_iteration: ok"
    LABELS "regression_gate")
# Exhausted built-in iterators raise StopIteration from __next__; heapq.merge works.
add_test(NAME protopy_native_iterator_next
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/native_iterator_next.py)
set_tests_properties(protopy_native_iterator_next PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# A package's __path__ is a list holding its directory, shared with its spec.
add_test(NAME protopy_package_path_list
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/package_path_list.py)
set_tests_properties(protopy_package_path_list PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "package_path_list: ok"
    LABELS "regression_gate")
# import site is silent: next(it, default), sys.copyright and ImportError.name.
add_test(NAME protopy_import_site
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/import_site.py)
set_tests_properties(protopy_import_site PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# object.__setattr__ is object's own store, not the class's __setattr__ override.
add_test(NAME protopy_object_setattr_generic
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/object_setattr_generic.py)
set_tests_properties(protopy_object_setattr_generic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "object_setattr_generic: ok"
    LABELS "regression_gate")
# Frame f_locals / f_lineno / f_back, exec() frames and the setuptools nspkg .pth line.
add_test(NAME protopy_frame_f_locals
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/frame_f_locals.py)
set_tests_properties(protopy_frame_f_locals PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Lists the runtime builds natively (os.listdir, re.split, ...) are mutable.
add_test(NAME protopy_native_lists_mutable
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/native_lists_mutable.py)
set_tests_properties(protopy_native_lists_mutable PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "native_lists_mutable: ok"
    LABELS "regression_gate")
# io.open_code opens in binary, as site.addpackage expects of a .pth file.
add_test(NAME protopy_io_open_code
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/io_open_code.py)
set_tests_properties(protopy_io_open_code PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# agen.athrow() returns an awaitable that throws when driven.
add_test(NAME protopy_async_gen_athrow
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/async_gen_athrow.py)
set_tests_properties(protopy_async_gen_athrow PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "async_gen_athrow: ok"
    LABELS "regression_gate")
# sys.path_hooks / sys.path_importer_cache, PEP 420 namespace packages, an
# nspkg .pth namespace package and pkgutil.iter_modules.
add_test(NAME protopy_import_path_hooks
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/import_path_hooks.py)
set_tests_properties(protopy_import_path_hooks PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# await agen.aclose() closes an async generator and finishes.
add_test(NAME protopy_async_gen_aclose
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/async_gen_aclose.py)
set_tests_properties(protopy_async_gen_aclose PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "async_gen_aclose: ok"
    LABELS "regression_gate")
# An atexit handler that raises is reported as CPython reports it, and the
# other handlers still run with their keyword arguments.
add_test(NAME protopy_atexit_callback_error
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/atexit_callback_error.py)
set_tests_properties(protopy_atexit_callback_error PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "atexit_callback_error: registered\r?\nException ignored in atexit callback <function failing_handler at 0x[0-9a-f]+>:\r?\nTraceback \\(most recent call last\\):\r?\n  File [^\n]*atexit_callback_error\\.py\", line 12, in failing_handler\r?\n    raise ValueError\\(\"boom\"\\)\r?\n[^\n]*ValueError[^\n]*: boom[\r\n]+later_handler 1 3"
    LABELS "regression_gate")
# A .pth line importing importlib.util exits cleanly with HOME empty: atexit
# handlers run at interpreter exit, not at the end of the next import.
add_test(NAME protopy_pth_importlib_empty_home
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/pth_importlib_empty_home.py)
set_tests_properties(protopy_pth_importlib_empty_home PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    ENVIRONMENT "HOME="
    TIMEOUT 120
    FAIL_REGULAR_EXPRESSION "Exception ignored;atexit"
    LABELS "regression_gate")
# A generator suspended while building a call keeps the call's callable.
add_test(NAME protopy_yield_inside_call_args
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/yield_inside_call_args.py)
set_tests_properties(protopy_yield_inside_call_args PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "yield_inside_call_args: ok"
    LABELS "regression_gate")
# The builtins aiter() and anext().
add_test(NAME protopy_builtin_aiter_anext
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/builtin_aiter_anext.py)
set_tests_properties(protopy_builtin_aiter_anext PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "builtin_aiter_anext: ok"
    LABELS "regression_gate")
# A weakref.finalize alive at exit runs from its atexit handler (gc.isenabled).
add_test(NAME protopy_weakref_finalize_at_exit
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/weakref_finalize_at_exit.py)
set_tests_properties(protopy_weakref_finalize_at_exit PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "weakref_finalize_at_exit: finalizer ran"
    FAIL_REGULAR_EXPRESSION "Exception ignored;atexit:"
    LABELS "regression_gate")
# from module import name falls back to the module's __getattr__ (PEP 562).
add_test(NAME protopy_import_from_module_getattr
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/import_from_module_getattr.py)
set_tests_properties(protopy_import_from_module_getattr PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "import_from_module_getattr: ok"
    LABELS "regression_gate")
# Recursion past the native stack raises RecursionError.
add_test(NAME protopy_deep_recursion_raises
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/deep_recursion_raises.py)
set_tests_properties(protopy_deep_recursion_raises PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "deep_recursion_raises: ok"
    LABELS "regression_gate")
# A bound method answers __module__ and __doc__ from its function.
add_test(NAME protopy_bound_method_module_doc
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/bound_method_module_doc.py)
set_tests_properties(protopy_bound_method_module_doc PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "bound_method_module_doc: ok"
    LABELS "regression_gate")
# _sysconfig exists, so test.support imports on Windows.
add_test(NAME protopy_sysconfig_nt_vars
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/sysconfig_nt_vars.py)
set_tests_properties(protopy_sysconfig_nt_vars PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "sysconfig_nt_vars: ok"
    LABELS "regression_gate")
# Walking f_back ends in None at the main module.
add_test(NAME protopy_frame_chain_ends_in_none
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/frame_chain_ends_in_none.py)
set_tests_properties(protopy_frame_chain_ends_in_none PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "frame_chain_ends_in_none: ok"
    LABELS "regression_gate")
# function.__annotations__ comes from __annotate__.
add_test(NAME protopy_function_annotations_from_annotate
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/function_annotations_from_annotate.py)
set_tests_properties(protopy_function_annotations_from_annotate PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "function_annotations_from_annotate: ok"
    LABELS "regression_gate")
# platform.python_version() parses sys.version.
add_test(NAME protopy_platform_version
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/platform_version.py)
set_tests_properties(protopy_platform_version PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# os.pipe is non-inheritable; Popen with a stdin pipe does not wait for the child.
add_test(NAME protopy_os_pipe_subprocess_stdin
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/os_pipe_subprocess_stdin.py)
set_tests_properties(protopy_os_pipe_subprocess_stdin PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "os_pipe_subprocess_stdin: ok"
    LABELS "regression_gate")
# A bound builtin method stored on a class keeps its receiver.
add_test(NAME protopy_bound_builtin_class_attr
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/bound_builtin_class_attr.py)
set_tests_properties(protopy_bound_builtin_class_attr PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "bound_builtin_class_attr: ok"
    LABELS "regression_gate")
# test.support's helpers import on every platform.
add_test(NAME protopy_test_support_helpers_import
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/test_support_helpers_import.py)
set_tests_properties(protopy_test_support_helpers_import PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "test_support_helpers_import: ok"
    LABELS "regression_gate")
# os.execv/execve reject bad argument lists and environments with ValueError.
add_test(NAME protopy_os_exec_argument_checks
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/os_exec_argument_checks.py)
set_tests_properties(protopy_os_exec_argument_checks PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "os_exec_argument_checks: ok"
    LABELS "regression_gate")
# Calling an instance with its own __code__ runs __call__.
add_test(NAME protopy_callable_with_code_attr
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/callable_with_code_attr.py)
set_tests_properties(protopy_callable_with_code_attr PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "callable_with_code_attr: ok"
    LABELS "regression_gate")
# import pathlib loads the standard library package (Path is callable).
add_test(NAME protopy_pathlib_package
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/pathlib_package.py)
set_tests_properties(protopy_pathlib_package PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# operator applies the language's operators.
add_test(NAME protopy_operator_module
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/operator_module.py)
set_tests_properties(protopy_operator_module PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "operator_module: ok"
    LABELS "regression_gate")
# copy.copy of tuple/str/bytes/int/float subclass instances keeps the value.
add_test(NAME protopy_copy_builtin_subclass
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/copy_builtin_subclass.py)
set_tests_properties(protopy_copy_builtin_subclass PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Comparison operators find rich comparison methods defined on a base class.
add_test(NAME protopy_compare_inherited_dunders
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/compare_inherited_dunders.py)
set_tests_properties(protopy_compare_inherited_dunders PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# OS errors and OSError(errno, ...) give the errno-specific OSError subclass.
add_test(NAME protopy_oserror_errno_subclass
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/oserror_errno_subclass.py)
set_tests_properties(protopy_oserror_errno_subclass PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# open() with a path in "w", "a" or "x" mode writes to the file.
add_test(NAME protopy_io_open_write_modes
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/io_open_write_modes.py)
set_tests_properties(protopy_io_open_write_modes PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# io.text_encoding follows CPython; Path.read_text and write_text work.
add_test(NAME protopy_io_text_encoding
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/io_text_encoding.py)
set_tests_properties(protopy_io_text_encoding PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# os.DirEntry.is_junction exists; os.walk, shutil.rmtree and TemporaryDirectory work.
add_test(NAME protopy_direntry_is_junction
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/direntry_is_junction.py)
set_tests_properties(protopy_direntry_is_junction PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# concurrent.futures.thread imports and its executor runs work.
add_test(NAME protopy_concurrent_futures_thread
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/concurrent_futures_thread.py)
set_tests_properties(protopy_concurrent_futures_thread PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "concurrent_futures_thread: ok"
    LABELS "regression_gate")
# `return` from inside a loop that runs inside a `with` block drops the loop's
# iterator before the with-block cleanup runs.
add_test(NAME protopy_with_return_in_loop
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/with_return_in_loop.py)
set_tests_properties(protopy_with_return_in_loop PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Text mode reads with universal newlines (CRLF and CR read as LF), as CPython.
add_test(NAME protopy_universal_newlines
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/universal_newlines.py)
set_tests_properties(protopy_universal_newlines PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Strings computed by `re` are collectable objects, not interned symbols.
# Run under a heap limit so the collector actually runs and the RSS bound in
# the test means something: without a limit protoCore never starts a cycle.
add_test(NAME protopy_re_results_not_interned
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/re_results_not_interned.py)
set_tests_properties(protopy_re_results_not_interned PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    ENVIRONMENT "PROTOCORE_HEAP_LIMIT_CELLS=500000"
    TIMEOUT 120
    LABELS "regression_gate")
# Lines read from a file and str method results are collectable, not interned:
# interning them made line-by-line reading superlinear.
add_test(NAME protopy_file_lines_not_interned
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/file_lines_not_interned.py)
set_tests_properties(protopy_file_lines_not_interned PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Reading a file by lines is linear in the line count. Clock-dependent: CI runs
# it in the timing job (CLOCK_DEPENDENT_TESTS in the workflows).
add_test(NAME protopy_file_lines_linear
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/file_lines_linear.py)
set_tests_properties(protopy_file_lines_linear PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120)
# io.StringIO counts characters: read(n), readline(n), seek(), tell(),
# truncate() and write()'s result are character counts and indices.
add_test(NAME protopy_stringio_char_positions
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/stringio_char_positions.py)
set_tests_properties(protopy_stringio_char_positions PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# re's ^, $, \A, \Z and . follow CPython's line rules on every platform.
# re on PCRE2: the documented differences from CPython are exceptions, never
# silently wrong results (engine limits, \N{...}, lone surrogates, ...).
# Under a fixed 500,000-cell heap: its 100 KB subject needed about 3.8 M cells
# while len() converted a str into a list of its characters.
add_test(NAME protopy_re_pcre2_limits
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/re_pcre2_limits.py)
set_tests_properties(protopy_re_pcre2_limits PROPERTIES
    ENVIRONMENT "PROTOCORE_ADAPTIVE_HEAP=0;PROTOCORE_HEAP_LIMIT_CELLS=500000"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# A 100 KB str, built and used the common ways, fits a fixed 2,000,000-cell
# heap (about 1.25 M needed; 12.6 M before strings stopped being exploded into
# lists and generator bookkeeping stopped piling up in native consumers).
add_test(NAME protopy_str_cells_bounded
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/str_cells_bounded.py)
set_tests_properties(protopy_str_cells_bounded PROPERTIES
    ENVIRONMENT "PROTOCORE_ADAPTIVE_HEAP=0;PROTOCORE_HEAP_LIMIT_CELLS=2000000"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 300
    LABELS "regression_gate")
add_test(NAME protopy_re_anchors
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/re_anchors.py)
set_tests_properties(protopy_re_anchors PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# time/datetime conversions before 1970 and after 3000 answer or raise, never crash.
add_test(NAME protopy_time_out_of_range
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/time_out_of_range.py)
set_tests_properties(protopy_time_out_of_range PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# tempfile: NamedTemporaryFile, TemporaryFile, TemporaryDirectory round trips.
add_test(NAME protopy_tempfile_roundtrip
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/tempfile_roundtrip.py)
set_tests_properties(protopy_tempfile_roundtrip PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Unclosed files release their descriptor when collected (under a heap limit, so
# protoCore collects).
add_test(NAME protopy_file_close_on_collect
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/file_close_on_collect.py)
set_tests_properties(protopy_file_close_on_collect PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    ENVIRONMENT "PROTOCORE_HEAP_LIMIT_CELLS=1000000"
    TIMEOUT 120
    LABELS "regression_gate")
# seek/tell/seekable/readable/writable on every kind of file object; source
# lines in traceback.format_exc() (tokenize.open rewinds the file).
add_test(NAME protopy_file_seek_tell
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/file_seek_tell.py)
set_tests_properties(protopy_file_seek_tell PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "file_seek_tell: ok"
    TIMEOUT 120
    LABELS "regression_gate")
# Functions with annotations have an __annotate__ function (PEP 649/749), so
# functools.singledispatch's plain @register works.
add_test(NAME protopy_function_annotate
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/function_annotate.py)
set_tests_properties(protopy_function_annotate PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "function_annotate: ok"
    TIMEOUT 120
    LABELS "regression_gate")
# __main__.__file__ is absolute and sys.argv[0] is the path as given (the
# script is run by a relative path); `-c` code has no __main__.__file__.
add_test(NAME protopy_main_file_absolute
    COMMAND $<TARGET_FILE:protopy> test/regression/main_file_absolute.py)
set_tests_properties(protopy_main_file_absolute PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "main_file_absolute: ok"
    TIMEOUT 120
    LABELS "regression_gate")
add_test(NAME protopy_c_main_has_no_file
    COMMAND $<TARGET_FILE:protopy> -c
            "import __main__, sys; assert not hasattr(__main__, '__file__'), __main__.__file__; assert sys.argv[0] == '-c', sys.argv; print('c_main_has_no_file: ok')")
set_tests_properties(protopy_c_main_has_no_file PROPERTIES
    PASS_REGULAR_EXPRESSION "c_main_has_no_file: ok"
    TIMEOUT 120
    LABELS "regression_gate")
# sys.stdin and input() read standard input as CPython does: protopy runs with
# its standard input redirected from files (test/regression/cli_stdin.cmake).
add_test(NAME protopy_cli_stdin
    COMMAND ${CMAKE_COMMAND} -DPROTOPY=$<TARGET_FILE:protopy>
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli_stdin_work
            -P ${CMAKE_SOURCE_DIR}/test/regression/cli_stdin.cmake)
set_tests_properties(protopy_cli_stdin PROPERTIES
    PASS_REGULAR_EXPRESSION "cli_stdin: ok"
    TIMEOUT 180
    LABELS "regression_gate")
# sys.path[0] for a script, -m and -c, and -P (test/regression/cli_sys_path.cmake).
add_test(NAME protopy_cli_sys_path
    COMMAND ${CMAKE_COMMAND} -DPROTOPY=$<TARGET_FILE:protopy>
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli_sys_path_work
            -P ${CMAKE_SOURCE_DIR}/test/regression/cli_sys_path.cmake)
set_tests_properties(protopy_cli_sys_path PROPERTIES
    PASS_REGULAR_EXPRESSION "cli_sys_path: ok"
    TIMEOUT 180
    LABELS "regression_gate")
# Objects held only by running frames survive heap-limited collections: every
# context is chained onto the thread's current one (MemoryManager.hpp). The
# cross-platform workflow runs it repeatedly.
add_test(NAME protopy_heap_limit_live_frames
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/heap_limit_live_frames.py)
set_tests_properties(protopy_heap_limit_live_frames PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    ENVIRONMENT "PROTOCORE_HEAP_LIMIT_CELLS=1000000"
    PASS_REGULAR_EXPRESSION "heap_limit_live_frames: ok"
    TIMEOUT 120
    LABELS "regression_gate")
# Directories of files that were written and never closed can be removed.
add_test(NAME protopy_unclosed_files_directory_cleanup
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/unclosed_files_directory_cleanup.py)
set_tests_properties(protopy_unclosed_files_directory_cleanup PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Text-mode writes translate "\n" to os.linesep, honouring newline=.
add_test(NAME protopy_text_newline_write
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/text_newline_write.py)
set_tests_properties(protopy_text_newline_write PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# select.select on pipes: timeouts, readiness, ValueError for unrepresentable sets.
add_test(NAME protopy_select_pipes
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/select_pipes.py)
set_tests_properties(protopy_select_pipes PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# os.symlink, lstat and shutil.copytree/rmtree over symbolic links.
add_test(NAME protopy_copytree_symlinks
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/copytree_symlinks.py)
set_tests_properties(protopy_copytree_symlinks PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# bool(), `if` / `while` and the native truth test agree on every shape.
add_test(NAME protopy_truth_tests
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/truth_tests.py)
set_tests_properties(protopy_truth_tests PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# random.Random(seed) replays the same sequence.
add_test(NAME protopy_random_seed_reproducible
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/random_seed_reproducible.py)
set_tests_properties(protopy_random_seed_reproducible PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# A directory added to sys.path at runtime is searched by import.
add_test(NAME protopy_sys_path_runtime_insert
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/sys_path_runtime_insert.py)
set_tests_properties(protopy_sys_path_runtime_insert PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Ordering comparisons on plain objects raise TypeError, as in CPython.
add_test(NAME protopy_object_ordering_typeerror
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/object_ordering_typeerror.py)
set_tests_properties(protopy_object_ordering_typeerror PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# frozenset carries the non-mutating set methods (union, issubset, isdisjoint...).
add_test(NAME protopy_frozenset_set_methods
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/frozenset_set_methods.py)
set_tests_properties(protopy_frozenset_set_methods PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Methods read from a frozenset instance are bound to it.
add_test(NAME protopy_frozenset_methods_bound
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/frozenset_methods_bound.py)
set_tests_properties(protopy_frozenset_methods_bound PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# copy and pickle of set and frozenset subclass instances keep their elements.
add_test(NAME protopy_copy_set_subclass
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/copy_set_subclass.py)
set_tests_properties(protopy_copy_set_subclass PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# operator.eq, ne, lt, le, gt and ge compare like the operators, for any operand type.
add_test(NAME protopy_operator_rich_compare
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/operator_rich_compare.py)
set_tests_properties(protopy_operator_rich_compare PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# operator.contains(a, b) is `b in a`.
add_test(NAME protopy_operator_contains
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/operator_contains.py)
set_tests_properties(protopy_operator_contains PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# collections.deque.count counts equal elements.
add_test(NAME protopy_deque_count
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/deque_count.py)
set_tests_properties(protopy_deque_count PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# platform.python_implementation() agrees with sys.implementation.name.
add_test(NAME protopy_platform_implementation
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/platform_implementation.py)
set_tests_properties(protopy_platform_implementation PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# The array module stores typed values.
add_test(NAME protopy_array_module
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/array_module.py)
set_tests_properties(protopy_array_module PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "array_module: ok"
    LABELS "regression_gate")
# sys.version carries the build date generated at configure time.
add_test(NAME protopy_sys_version_build_date
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/sys_version_build_date.py
            "${PROTOPYTHON_BUILD_DATE}")
set_tests_properties(protopy_sys_version_build_date PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# `from module import missing` raises ImportError with CPython's message, name and path.
add_test(NAME protopy_import_from_error_message
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/import_from_error_message.py)
set_tests_properties(protopy_import_from_error_message PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# str indexing, slicing, iteration, searching, padding and repr work in code points.
add_test(NAME protopy_str_non_ascii
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/str_non_ascii.py)
set_tests_properties(protopy_str_non_ascii PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Equal ints and floats compare, hash and key dicts as one value beyond long long.
add_test(NAME protopy_numeric_keys
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/numeric_keys.py)
set_tests_properties(protopy_numeric_keys PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# `d[k] += v` and `obj.attr += v` store the result, evaluating the target once.
add_test(NAME protopy_aug_assign_targets
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/aug_assign_targets.py)
set_tests_properties(protopy_aug_assign_targets PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# The peephole specialiser leaves alone a window a jump enters part-way.
add_test(NAME protopy_peephole_jump_targets
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/peephole_jump_targets.py)
set_tests_properties(protopy_peephole_jump_targets PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# No statement leaves a stray POP_TOP (inside a loop it popped the iterator: a hang).
add_test(NAME protopy_statement_stack_balance
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/statement_stack_balance.py)
set_tests_properties(protopy_statement_stack_balance PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# `match` binds captures in the enclosing scope, hides its temporaries, works in loops.
add_test(NAME protopy_match_scoping
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/match_scoping.py)
set_tests_properties(protopy_match_scoping PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# `<<`, `>>`, `<<=` and `>>=` on ints of any size.
add_test(NAME protopy_int_shift_big
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/int_shift_big.py)
set_tests_properties(protopy_int_shift_big PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# Augmented assignment calls in-place dunders defined in Python.
add_test(NAME protopy_inplace_user_dunders
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/inplace_user_dunders.py)
set_tests_properties(protopy_inplace_user_dunders PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# A starred assignment target binds in the enclosing scope.
add_test(NAME protopy_starred_target_scope
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/starred_target_scope.py)
set_tests_properties(protopy_starred_target_scope PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# io.StringIO and io.BytesIO can be subclassed.
add_test(NAME protopy_io_memory_subclass
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/io_memory_subclass.py)
set_tests_properties(protopy_io_memory_subclass PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "io_memory_subclass: ok"
    LABELS "regression_gate")
# Asynchronous comprehensions finish and give their values.
add_test(NAME protopy_async_comprehension
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/async_comprehension.py)
set_tests_properties(protopy_async_comprehension PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# Built-ins called with too few arguments raise TypeError.
add_test(NAME protopy_builtin_arity
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/builtin_arity.py)
set_tests_properties(protopy_builtin_arity PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# _opcode names every opcode and computes stack effects.
add_test(NAME protopy_opcode_module
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/opcode_module.py)
set_tests_properties(protopy_opcode_module PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# dict subclasses take their own __init__ arguments; dict.__init__ applies kwargs.
add_test(NAME protopy_dict_subclass_init
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/dict_subclass_init.py)
set_tests_properties(protopy_dict_subclass_init PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# collections.defaultdict is a working dict subclass.
add_test(NAME protopy_defaultdict_basic
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/defaultdict_basic.py)
set_tests_properties(protopy_defaultdict_basic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# sys.setswitchinterval / sys.getswitchinterval exist and validate like CPython.
add_test(NAME protopy_sys_switchinterval
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/sys_switchinterval.py)
set_tests_properties(protopy_sys_switchinterval PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# type's getset descriptors resolve on classes (type.__qualname__); their repr works.
add_test(NAME protopy_type_getset_descriptors
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/type_getset_descriptors.py)
set_tests_properties(protopy_type_getset_descriptors PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# tuple.index / tuple.count work on raw tuples such as *args.
add_test(NAME protopy_tuple_index_raw
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/tuple_index_raw.py)
set_tests_properties(protopy_tuple_index_raw PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# `import asyncio` works; socketpair, sys asyncgen hooks, signal.default_int_handler, itertools.chain exist.
add_test(NAME protopy_asyncio_run_basic
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/asyncio_run_basic.py)
set_tests_properties(protopy_asyncio_run_basic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Class creation honours PEP 560 __mro_entries__ and records __orig_bases__.
add_test(NAME protopy_mro_entries
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/mro_entries.py)
set_tests_properties(protopy_mro_entries PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# Exceptions from __init_subclass__ or the metaclass are catchable.
add_test(NAME protopy_init_subclass_exception
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/init_subclass_exception.py)
set_tests_properties(protopy_init_subclass_exception PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# str.format_map reads names from the mapping.
add_test(NAME protopy_str_format_map
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/str_format_map.py)
set_tests_properties(protopy_str_format_map PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "str_format_map: ok"
    LABELS "regression_gate")
# Sets key elements like dict keys: __hash__, 1 == 1.0 == True, tuples by elements, unhashables raise.
add_test(NAME protopy_set_python_hash
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/set_python_hash.py)
set_tests_properties(protopy_set_python_hash PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Native callables (len, [].append) are instances of types.BuiltinFunctionType & co.
add_test(NAME protopy_isinstance_native_methods
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/isinstance_native_methods.py)
set_tests_properties(protopy_isinstance_native_methods PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Unhashable dict keys raise TypeError; non-str/int keys are looked up by hash.
add_test(NAME protopy_dict_unhashable_keys
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/dict_unhashable_keys.py)
set_tests_properties(protopy_dict_unhashable_keys PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Set operators with a dict view on the left use the set's reflected operators.
add_test(NAME protopy_set_reflected_ops
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/set_reflected_ops.py)
set_tests_properties(protopy_set_reflected_ops PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# A class defining __eq__ without __hash__ is unhashable.
add_test(NAME protopy_class_eq_unhashable
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/class_eq_unhashable.py)
set_tests_properties(protopy_class_eq_unhashable PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Zero-argument __class__ in methods is the class the statement creates.
add_test(NAME protopy_class_cell
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/class_cell.py)
set_tests_properties(protopy_class_cell PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Comparison operators return the dunder's result as is; unsupported orderings raise TypeError.
add_test(NAME protopy_rich_compare_results
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/rich_compare_results.py)
set_tests_properties(protopy_rich_compare_results PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# `del name` unbinds the name in module, class, global and exec() scopes.
add_test(NAME protopy_del_name
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/del_name.py)
set_tests_properties(protopy_del_name PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Fast locals start unbound: reading or deleting one before assignment raises.
add_test(NAME protopy_unbound_fast_locals
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/unbound_fast_locals.py)
set_tests_properties(protopy_unbound_fast_locals PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# A script's main() runs only when the script calls it, as in CPython.
add_test(NAME protopy_main_not_auto_invoked
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/main_not_auto_invoked.py)
set_tests_properties(protopy_main_not_auto_invoked PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# SystemExit.code follows CPython (None / args[0] / args), including sys.exit().
add_test(NAME protopy_system_exit_code
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/system_exit_code.py)
set_tests_properties(protopy_system_exit_code PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# gi_suspended and cr_suspended track a generator stopped at a yield.
add_test(NAME protopy_generator_suspended_state
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/generator_suspended_state.py)
set_tests_properties(protopy_generator_suspended_state PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "generator_suspended_state: ok"
    LABELS "regression_gate")
# StopIteration escaping a generator body becomes RuntimeError (PEP 479).
add_test(NAME protopy_generator_stopiteration_pep479
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/generator_stopiteration_pep479.py)
set_tests_properties(protopy_generator_stopiteration_pep479 PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "generator_stopiteration_pep479: ok"
    LABELS "regression_gate")
# An uncaught SystemExit sets the process exit status: an int is the status,
# None is 0, anything else is printed to stderr and exits with 1.
foreach(exit_case IN ITEMS raise_int:3 raise_from_main:5 sys_exit_int:4 raise_str:1 sys_exit_str:1 raise_bare:0)
    string(REPLACE ":" ";" exit_case_parts ${exit_case})
    list(GET exit_case_parts 0 exit_case_name)
    list(GET exit_case_parts 1 exit_case_status)
    add_test(NAME protopy_system_exit_${exit_case_name}
        COMMAND ${PROTOPYTHON_ASSERT_EXIT} ${exit_case_status}
                $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/system_exit/${exit_case_name}.py)
    set_tests_properties(protopy_system_exit_${exit_case_name} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 120
        LABELS "regression_gate")
endforeach()
# The same exit status rules apply to `protopy -c`; any other uncaught
# exception still exits with 70.
function(add_system_exit_c_test case_name expected_status source)
    add_test(NAME protopy_system_exit_c_${case_name}
        COMMAND ${PROTOPYTHON_ASSERT_EXIT} ${expected_status}
                $<TARGET_FILE:protopy> -c ${source})
    set_tests_properties(protopy_system_exit_c_${case_name} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 120
        LABELS "regression_gate")
endfunction()
add_system_exit_c_test(raise_int 3 "raise SystemExit(3)")
add_system_exit_c_test(sys_exit_int 4 "__import__('sys').exit(4)")
add_system_exit_c_test(raise_bare 0 "raise SystemExit")
add_system_exit_c_test(sys_exit_str 1 "__import__('sys').exit('bye')")
add_system_exit_c_test(no_exception 0 "print(1)")
add_system_exit_c_test(other_exception 70 "raise ValueError('x')")
# Class statements accept *bases and **keywords.
add_test(NAME protopy_class_star_bases
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/class_star_bases.py)
set_tests_properties(protopy_class_star_bases PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Starred expressions in subscripts (PEP 646) build the index tuple.
add_test(NAME protopy_subscript_starred
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/subscript_starred.py)
set_tests_properties(protopy_subscript_starred PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# PEP 695 type parameters on classes, functions and `type` aliases.
add_test(NAME protopy_pep695_type_params
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/pep695_type_params.py)
set_tests_properties(protopy_pep695_type_params PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# async generics, `type`/`async def` in functions, duplicate class keywords, default __type_params__.
add_test(NAME protopy_pep695_extras
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/pep695_extras.py)
set_tests_properties(protopy_pep695_extras PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# exec()/eval() run compile() code objects in the right namespaces.
add_test(NAME protopy_exec_code_object
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/exec_code_object.py)
set_tests_properties(protopy_exec_code_object PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# compile(..., "single") echoes expression values through sys.displayhook.
add_test(NAME protopy_compile_single
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/compile_single.py)
set_tests_properties(protopy_compile_single PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# globals(), vars(module) and module.__dict__ are the module's namespace mapping.
add_test(NAME protopy_module_namespace_mapping
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/module_namespace_mapping.py)
set_tests_properties(protopy_module_namespace_mapping PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# A str is not a list: isinstance, list operators and methods, item assignment.
add_test(NAME protopy_str_is_not_list
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/str_is_not_list.py)
set_tests_properties(protopy_str_is_not_list PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# len() of a str is O(1); str.__len__ exists.
add_test(NAME protopy_str_len_constant_time
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/str_len_constant_time.py)
set_tests_properties(protopy_str_len_constant_time PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# \N{NAME} escapes in str literals; unknown escapes keep their backslash.
add_test(NAME protopy_str_named_escape
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/str_named_escape.py)
set_tests_properties(protopy_str_named_escape PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# list.__init__ fills the list; subclasses receive their own arguments.
add_test(NAME protopy_list_subclass_init
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/list_subclass_init.py)
set_tests_properties(protopy_list_subclass_init PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# The native reduce follows CPython's signature.
add_test(NAME protopy_functools_reduce_args
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/functools_reduce_args.py)
set_tests_properties(protopy_functools_reduce_args PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "functools_reduce_args: ok"
    LABELS "regression_gate")
# __init_subclass__ runs once for type(), types.new_class() and metaclasses.
add_test(NAME protopy_init_subclass_dynamic
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/init_subclass_dynamic.py)
set_tests_properties(protopy_init_subclass_dynamic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Unconsumed class keywords make object.__init_subclass__ raise TypeError.
add_test(NAME protopy_init_subclass_keywords
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/init_subclass_keywords.py)
set_tests_properties(protopy_init_subclass_keywords PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# reversed() works for lists, tuples, subclasses and dict views.
add_test(NAME protopy_reversed_native
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/reversed_native.py)
set_tests_properties(protopy_reversed_native PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# OrderedDict is a dict subclass keeping insertion order: move_to_end, popitem, repr, |.
add_test(NAME protopy_ordereddict_basic
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/ordereddict_basic.py)
set_tests_properties(protopy_ordereddict_basic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# deque subclasses, deque.__init__ and maxlen.
add_test(NAME protopy_deque_subclass
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/deque_subclass.py)
set_tests_properties(protopy_deque_subclass PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# str() of a dict subclass uses the subclass's __repr__.
add_test(NAME protopy_dict_subclass_str
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/dict_subclass_str.py)
set_tests_properties(protopy_dict_subclass_str PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# map(func, *iterables, strict=True) raises ValueError on unequal lengths.
add_test(NAME protopy_map_strict
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/map_strict.py)
set_tests_properties(protopy_map_strict PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# sorted() accepts builtin subclasses and Python-level iterators.
add_test(NAME protopy_sorted_iterables
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/sorted_iterables.py)
set_tests_properties(protopy_sorted_iterables PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# set subclasses fill through set.__init__ and iterate.
add_test(NAME protopy_set_subclass_init
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/set_subclass_init.py)
set_tests_properties(protopy_set_subclass_init PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# TypeVar & co. record their module and pickle by name; Box[int] repr; isinstance with tuples.
add_test(NAME protopy_typing_module_attrs
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/typing_module_attrs.py)
set_tests_properties(protopy_typing_module_attrs PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# dict.clear/popitem called unbound act on the instance; dict.copy() returns a dict.
add_test(NAME protopy_dict_unbound_methods
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/dict_unbound_methods.py)
set_tests_properties(protopy_dict_unbound_methods PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Code flags use CPython's numbering.
add_test(NAME protopy_code_flags_cpython
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/code_flags_cpython.py)
set_tests_properties(protopy_code_flags_cpython PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "code_flags_cpython: ok"
    LABELS "regression_gate")
# list.__init__ clears the list before extending it: x.__init__(x) empties x.
add_test(NAME protopy_list_init_self
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/list_init_self.py)
set_tests_properties(protopy_list_init_self PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# deque comparisons, __hash__ = None and read-only maxlen.
add_test(NAME protopy_deque_compare
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/deque_compare.py)
set_tests_properties(protopy_deque_compare PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# cls[X] uses the metaclass __getitem__ before __class_getitem__ (Enum C['X']).
add_test(NAME protopy_metaclass_getitem
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/metaclass_getitem.py)
set_tests_properties(protopy_metaclass_getitem PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# A metaclass belongs to the class object: its instances neither are instances
# of it nor see its attributes.
add_test(NAME protopy_metaclass_not_inherited
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/metaclass_not_inherited.py)
set_tests_properties(protopy_metaclass_not_inherited PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# print(), format() and str.format() use a __str__ / __format__ written in Python.
add_test(NAME protopy_print_python_str
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/print_python_str.py)
set_tests_properties(protopy_print_python_str PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# enum.Flag / IntFlag |, &, ^, ~ and F(value) build composite members (STRICT/KEEP bound, _all_bits_ set).
add_test(NAME protopy_enum_flag_ops
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/enum_flag_ops.py)
set_tests_properties(protopy_enum_flag_ops PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# deque mutators from parallel threads keep every item once; items are GC-traced.
add_test(NAME protopy_deque_threads
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/deque_threads.py)
set_tests_properties(protopy_deque_threads PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# StopIteration.args / value hold the arguments, not the instance itself.
add_test(NAME protopy_stopiteration_value
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/stopiteration_value.py)
set_tests_properties(protopy_stopiteration_value PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# str(e) is str(e.args[0]) for one argument and str(e.args) otherwise.
add_test(NAME protopy_exception_str_args
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/exception_str_args.py)
set_tests_properties(protopy_exception_str_args PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# A class is an instance of its metaclass only: isinstance(str, str) is False.
add_test(NAME protopy_isinstance_class_objects
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/isinstance_class_objects.py)
set_tests_properties(protopy_isinstance_class_objects PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# *Ts is Unpack[Ts]: Generic[T, *Ts] collects Ts and substitutes variadic arguments.
add_test(NAME protopy_typevartuple_generic
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/typevartuple_generic.py)
set_tests_properties(protopy_typevartuple_generic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# NotImplemented and Ellipsis print and repr as their names.
add_test(NAME protopy_notimplemented_repr
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/notimplemented_repr.py)
set_tests_properties(protopy_notimplemented_repr PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Orderings nobody implements raise TypeError, also inside containers and sorted().
add_test(NAME protopy_ordering_type_errors
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/ordering_type_errors.py)
set_tests_properties(protopy_ordering_type_errors PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# str(KeyError(k)) is repr(k).
add_test(NAME protopy_keyerror_str
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/keyerror_str.py)
set_tests_properties(protopy_keyerror_str PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# A keyword given twice in a call raises TypeError naming the callee.
add_test(NAME protopy_call_duplicate_keywords
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/call_duplicate_keywords.py)
set_tests_properties(protopy_call_duplicate_keywords PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Names assigned on the builtins module or any module are visible to name lookups.
add_test(NAME protopy_builtins_attributes
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/builtins_attributes.py)
set_tests_properties(protopy_builtins_attributes PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# A module's globals are its namespace, not the attributes every module inherits.
add_test(NAME protopy_module_global_names
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/module_global_names.py)
set_tests_properties(protopy_module_global_names PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# bytes compare only with bytes-like objects: b'a' == 'a' is False, b'a' < 'a' raises.
add_test(NAME protopy_bytes_str_compare
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/bytes_str_compare.py)
set_tests_properties(protopy_bytes_str_compare PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# bytes.find / rfind / index / rindex / count reject a needle that is neither an
# integer nor bytes-like, with CPython's TypeError (the dead `== nullptr` guard).
add_test(NAME protopy_bytes_find_needle_type
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/bytes_find_needle_type.py)
set_tests_properties(protopy_bytes_find_needle_type PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# A bare module name is a NameError until imported; prototype attributes are not names.
add_test(NAME protopy_global_names_no_import
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/global_names_no_import.py)
set_tests_properties(protopy_global_names_no_import PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# list, tuple and dict __eq__ answer NotImplemented for other types.
add_test(NAME protopy_container_eq_notimplemented
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/container_eq_notimplemented.py)
set_tests_properties(protopy_container_eq_notimplemented PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# delattr(obj, name) deletes like del obj.name.
add_test(NAME protopy_delattr_removes
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/delattr_removes.py)
set_tests_properties(protopy_delattr_removes PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# asyncio.run() drives programs with sleep, gather, create_task and exceptions.
add_test(NAME protopy_asyncio_run_programs
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/asyncio_run_programs.py)
set_tests_properties(protopy_asyncio_run_programs PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# nonlocal rebinding reaches the enclosing function (closures share the binding).
add_test(NAME protopy_nonlocal_rebinding
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/nonlocal_rebinding.py)
set_tests_properties(protopy_nonlocal_rebinding PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Importing a submodule first loads its package before the submodule.
add_test(NAME protopy_submodule_import_first
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/submodule_import_first.py)
set_tests_properties(protopy_submodule_import_first PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# itertools.filterfalse is callable, so @dataclass(slots=True) works.
add_test(NAME protopy_dataclass_slots
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/dataclass_slots.py)
set_tests_properties(protopy_dataclass_slots PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# function.__closure__ is a tuple of cells and types.CellType is the cell type.
add_test(NAME protopy_closure_cells
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/closure_cells.py)
set_tests_properties(protopy_closure_cells PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Closures as CPython runs them (closure_semantics.expected is CPython's
# output): cells shared by the defining scope and the closures (late binding,
# nonlocal, del), recursive and mutually recursive nested functions, class
# bodies (their free variables, the methods' __class__ cell, zero-argument
# super()), comprehensions, generators, coroutines, __closure__, f_locals,
# locals() and eval() in a function with cells.  Also under a heap limit with
# collections forced at every safepoint.
add_test(NAME protopy_closure_semantics
    COMMAND ${CMAKE_COMMAND} -DPROTOPY=$<TARGET_FILE:protopy>
            -DSCRIPT=${CMAKE_SOURCE_DIR}/test/regression/closure_semantics.py
            -DEXPECTED=${CMAKE_SOURCE_DIR}/test/regression/closure_semantics.expected
            -P ${CMAKE_SOURCE_DIR}/test/regression/compare_output.cmake)
add_test(NAME protopy_closure_semantics_gc
    COMMAND ${CMAKE_COMMAND} -DPROTOPY=$<TARGET_FILE:protopy>
            -DSCRIPT=${CMAKE_SOURCE_DIR}/test/regression/closure_semantics.py
            -DEXPECTED=${CMAKE_SOURCE_DIR}/test/regression/closure_semantics.expected
            "-DENV=PROTOCORE_HEAP_LIMIT_CELLS=1500000;PROTOCORE_GC_CONTEXT_THRESHOLD=200"
            -P ${CMAKE_SOURCE_DIR}/test/regression/compare_output.cmake)
set_tests_properties(protopy_closure_semantics protopy_closure_semantics_gc PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# Standard-library behaviour checked against CPython: each <name>.py prints its
# results and <name>.expected is CPython 3.14's output for the same program.
#   ordered_dict_iterables: dict.update / OrderedDict / dict subclasses fed by
#     generators, iterators, zip, mappings with keys(), mappingproxy.
#   abc_virtual_subclasses: isinstance / issubclass with collections.abc
#     (registered built-in types, the ABC hierarchy, subclass hooks).
#   counter_methods: collections.Counter (most_common segfaulted) and the
#     deque iterators.
#   re_pcre2_semantics: the re module on PCRE2: pattern translation, the
#     Unicode and ASCII character sets, flags, str and bytes, positions, every
#     function and Match method, templates, errors and their positions.
#   regex_empty_matches: re with patterns that can match empty, classes with
#     a literal '-', group spans, named groups, Match / Pattern types.
#   argparse_nargs: argparse nargs variants (they rely on such patterns).
#   argparse_help: several prefix characters, help and usage output, textwrap.
#   bytes_construct_methods: bytes / bytearray from iterables, bytes methods.
#   hashlib_digests: every guaranteed digest, BLAKE2 parameters, SHAKE, hmac.
#   itertools_functions: every itertools function, None values, generator
#     inputs, laziness on infinite inputs, the combinatorics.
#   tuple_semantics: tuples (ProtoList-backed) against CPython: construction,
#     identity of (), hashing, equality, ordering, dict keys, sets, slicing,
#     unpacking, *args, namedtuple, pickle, subclasses.
#   attribute_access: attribute reads and writes on the one-lookup paths of
#     OP_LOAD_ATTR / OP_STORE_ATTR: descriptors, properties, methods,
#     __getattribute__ / __setattr__ defined on a base or assigned later,
#     super(), slots, metaclass attributes, deep hierarchies.
#   instantiation: the one-lookup instantiation path of runUserClassCall:
#     object.__new__'s checks, __init__ kinds, containers, exceptions, the
#     instance __dict__ created on demand.
#   exception_nesting: Python try / except / finally / with as C++ try /
#     catch: yields at every level, gen.throw / close, the handled exception
#     after a resume, chaining, suppression, break / continue through finally.
#   exc_info_semantics: the exception being handled (sys.exc_info(), a bare
#     raise, __context__) across callees, generators, finally and threads.
#   iteration_errors: errors raised while builtins iterate propagate (zip,
#     map, enumerate, filter, reversed, ...); Match.group; configparser.
#   collections_abc_mixins: the methods of the collections.abc ABCs (Set,
#     Mapping, Sequence, their mutable forms, the views, Generator, ...),
#     the exceptions they catch and let through, the ABC hierarchy,
#     __abstractmethods__ and abstract instantiation.
#   collections_abc_first_use_threads: threads calling ABC methods for the
#     first time at once, while the methods are being loaded.
set(PROTOPY_STDLIB_ORACLE_TESTS
    tuple_semantics
    attribute_access
    instantiation
    exception_nesting
    exc_info_semantics
    iteration_errors
    ordered_dict_iterables
    abc_virtual_subclasses
    collections_abc_mixins
    collections_abc_first_use_threads
    counter_methods
    regex_empty_matches
    re_pcre2_semantics
    argparse_nargs
    argparse_help
    bytes_construct_methods
    hashlib_digests
    itertools_functions)
foreach(_t IN LISTS PROTOPY_STDLIB_ORACLE_TESTS)
    add_test(NAME protopy_${_t}
        COMMAND ${CMAKE_COMMAND} -DPROTOPY=$<TARGET_FILE:protopy>
                -DSCRIPT=${CMAKE_SOURCE_DIR}/test/regression/${_t}.py
                -DEXPECTED=${CMAKE_SOURCE_DIR}/test/regression/${_t}.expected
                -P ${CMAKE_SOURCE_DIR}/test/regression/compare_output.cmake)
    set_tests_properties(protopy_${_t} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 120
        LABELS "regression_gate")
endforeach()
# A generator resume costs a small multiple of a function call, and so does
# each level a value crosses in a recursive `yield from` (both measured in the
# same process).  A resume copied every slot of the context, the operand
# stack area included, into new lists: ~70 us per resume.
add_test(NAME protopy_generator_resume_cost
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/generator_resume_cost.py)
set_tests_properties(protopy_generator_resume_cost PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 300
    LABELS "regression_gate")
# Tuples of fresh objects (*args, displays, dict.items(), zip, enumerate,
# divmod, tuple()) are collected: a loop of them runs in a fixed 1 M-cell heap.
# Tuples were interned protoCore ProtoTuples, never collected; this ran out of
# memory.  Tuple hashes keep the values they had.
add_test(NAME protopy_tuple_bounded_memory
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/tuple_bounded_memory.py 30000)
set_tests_properties(protopy_tuple_bounded_memory PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    ENVIRONMENT "PROTOCORE_HEAP_LIMIT_CELLS=1000000;PROTOCORE_ADAPTIVE_HEAP=0"
    TIMEOUT 600
    LABELS "regression_gate")
add_test(NAME protopy_tuple_hash_values
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/tuple_hash_values.py)
set_tests_properties(protopy_tuple_hash_values PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# Defining a named nested function (plain, recursive, nonlocal, capturing in a
# comprehension, returned) leaves nothing once the call returns: a loop of
# such calls runs in a fixed 2.5 M-cell heap.  Closures held the defining
# frame, a cycle of mutable objects per call that protoCore never collects;
# this ran out of memory.
add_test(NAME protopy_nested_def_bounded_memory
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/nested_def_bounded_memory.py 20000)
set_tests_properties(protopy_nested_def_bounded_memory PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    ENVIRONMENT "PROTOCORE_HEAP_LIMIT_CELLS=2500000;PROTOCORE_ADAPTIVE_HEAP=0"
    TIMEOUT 600
    LABELS "regression_gate")
# list, tuple and dict reprs show every item, and a self-containing one shows
# [...] / {...} (a per-thread recursion guard) instead of overflowing the stack.
add_test(NAME protopy_repr_containers
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/repr_containers.py)
set_tests_properties(protopy_repr_containers PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# The module type is <class 'module'>; its class attributes are not module globals.
add_test(NAME protopy_module_type
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/module_type.py)
set_tests_properties(protopy_module_type PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# `import typing` completes; Generic[T] subclasses, Protocol and aliases work.
add_test(NAME protopy_typing_import
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/typing_import.py)
set_tests_properties(protopy_typing_import PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 120
    LABELS "regression_gate")
# break / continue in a loop's else clause act on the enclosing loop.
add_test(NAME protopy_loop_else_break
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/loop_else_break.py)
set_tests_properties(protopy_loop_else_break PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# try / except / else / finally as in CPython: the finally clause runs however
# the statement is left, exceptions raised while handling another are chained,
# sys.exc_info() is cleared after the statement (every value is CPython's).
add_test(NAME protopy_try_finally_semantics
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/try_finally_semantics.py)
set_tests_properties(protopy_try_finally_semantics PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# weakref.ref hashes/compares like its referent: WeakKeyDictionary and the
# singledispatch dispatch cache find their entries.
add_test(NAME protopy_weakref_identity_keys
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/weakref_identity_keys.py)
set_tests_properties(protopy_weakref_identity_keys PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# Traceback entries carry the running code's file, function and current line
# (functions, modules, imported modules, exec/eval/compile code).
add_test(NAME protopy_traceback_locations
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/traceback_locations.py)
set_tests_properties(protopy_traceback_locations PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")
# An uncaught exception prints one entry per frame with its source line.
add_test(NAME protopy_traceback_uncaught
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/traceback_uncaught.py)
set_tests_properties(protopy_traceback_uncaught PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    PASS_REGULAR_EXPRESSION "Traceback \\(most recent call last\\):\r?\n  File \"[^\n]*traceback_uncaught\\.py\", line 14, in <module>\r?\n    outer\\(\\)\r?\n  File \"[^\n]*traceback_uncaught\\.py\", line 11, in outer\r?\n    return inner\\(1\\) \\+ 1\r?\n  File \"[^\n]*traceback_uncaught\\.py\", line 7, in inner\r?\n    return n / 0\r?\n[^\n]*ZeroDivisionError[^\n]*: division by zero"
    FAIL_REGULAR_EXPRESSION "<unknown>"
    LABELS "regression_gate")
# The _typing stub's Generic must compose with ABCMeta-derived metaclasses.
add_test(NAME protopy_typing_stub_generic
    COMMAND $<TARGET_FILE:protopy> ${CMAKE_SOURCE_DIR}/test/regression/typing_stub_generic.py)
set_tests_properties(protopy_typing_stub_generic PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 60
    LABELS "regression_gate")

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

# HPy extension modules used by the HPy loader tests: the math_hpy example and a
# library without an HPyInit_ entry point. Both are written to test/hpy in the
# build tree, which the tests put on the module search path.
foreach(hpy_module IN ITEMS math_hpy hpy_no_init)
    if(hpy_module STREQUAL "math_hpy")
        add_library(${hpy_module} MODULE examples/hpy/math_hpy.cpp)
        target_link_libraries(${hpy_module} PRIVATE protoPython protoCore)
    else()
        add_library(${hpy_module} MODULE test/hpy/hpy_no_init.cpp)
    endif()
    if(WIN32)
        # A DLL module (HPyModuleProvider looks for .hpy.dll there), with its
        # entry point exported. Windows places a MODULE's DLL with RUNTIME
        # output, so its directory is set too.
        set(_hpy_suffix ".hpy.dll")
        set_target_properties(${hpy_module} PROPERTIES
            WINDOWS_EXPORT_ALL_SYMBOLS ON
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test/hpy)
    else()
        set(_hpy_suffix ".hpy.so")
    endif()
    set_target_properties(${hpy_module} PROPERTIES
        PREFIX ""
        SUFFIX "${_hpy_suffix}"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test/hpy)
endforeach()
# `import math_hpy` loads the extension: name, file, functions, ImportError for a
# library without an entry point.
add_test(NAME protopy_hpy_module_basics
    COMMAND $<TARGET_FILE:protopy> -p ${CMAKE_BINARY_DIR}/test/hpy
            ${CMAKE_SOURCE_DIR}/test/hpy/hpy_module_basics.py)
set_tests_properties(protopy_hpy_module_basics PROPERTIES
    TIMEOUT 60
    LABELS "regression_gate")
# 20000 imports and calls of math_hpy.add under a hard heap limit, so the
# collector really runs while HPy handles are being created and recycled.
#
# A small PROTOCORE_HEAP_LIMIT_CELLS makes protoCore collect early: protopy's
# adaptive heap controller (README, "Memory") would otherwise start its first
# cycle only at its 128 MiB initial soft limit, which this run never reaches. The limit
# alone is not enough either — a cycle starts when a thread needs cells the
# heap cannot supply below the ceiling, so a run that allocates less than the
# heap it already holds never collects, whatever the ceiling is. At the
# previous 1000 iterations no ceiling down to 1000 cells produced a single
# cycle; at 20000 iterations a 100000-cell ceiling (about 6 MiB) produces five,
# in 0.23 s, with 20 of 20 runs passing.
add_test(NAME protopy_hpy_stress
    COMMAND $<TARGET_FILE:protopy> -p ${CMAKE_BINARY_DIR}/test/hpy
            ${CMAKE_SOURCE_DIR}/test/hpy/stress_test_hpy.py)
set_tests_properties(protopy_hpy_stress PROPERTIES
    ENVIRONMENT "PROTOCORE_HEAP_LIMIT_CELLS=100000"
    TIMEOUT 120
    LABELS "regression_gate")

# C++ embedding example: built by default and run by CTest so it keeps
# compiling against the current PythonEnvironment API. Its exit status is the
# check (it verifies the value it reads back from Python).
option(PROTOPYTHON_BUILD_EXAMPLES "Build and test the C++ embedding example" ON)
if(PROTOPYTHON_BUILD_EXAMPLES)
    add_executable(embedding_sample examples/embedding_sample.cpp)
    target_link_libraries(embedding_sample PRIVATE protoPython protoCore)
    add_test(NAME embedding_sample COMMAND embedding_sample)
    set_tests_properties(embedding_sample PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 60
        LABELS "regression_gate")
    # An embedding program keeps its own invalid-parameter handler (Windows);
    # a bad descriptor is an OSError, never the end of the process.
    add_executable(embedding_invalid_parameter_scope test/embedding/invalid_parameter_scope.cpp)
    target_link_libraries(embedding_invalid_parameter_scope PRIVATE protoPython protoCore)
    target_compile_definitions(embedding_invalid_parameter_scope PRIVATE
        STDLIB_PATH="${CMAKE_SOURCE_DIR}/lib/python3.14")
    add_test(NAME embedding_invalid_parameter_scope COMMAND embedding_invalid_parameter_scope)
    set_tests_properties(embedding_invalid_parameter_scope PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 60
        LABELS "regression_gate")
endif()

# --- protoCore's rule E3: an unselected exception leaves unchanged ------------
# test_exception_identity throws a proto::ProtoException subclass with an extra
# field through the paths of test/exceptions/exception_identity_probe.py (an
# except chain that matches nothing, finally, a bare raise, with, generators,
# nested calls) and checks that the same C++ object reaches it: interpreted,
# and compiled by protopyc (protopyc_exception_identity).
add_executable(test_exception_identity test/exceptions/test_exception_identity.cpp)
target_link_libraries(test_exception_identity PRIVATE protoPython protoCore)
target_compile_definitions(test_exception_identity PRIVATE
    STDLIB_PATH="${CMAKE_SOURCE_DIR}/lib/python3.14"
    PROBE_DIR="${CMAKE_SOURCE_DIR}/test/exceptions")
add_test(NAME exception_identity COMMAND test_exception_identity)
set_tests_properties(exception_identity PROPERTIES
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "exception_identity: PASS"
    FAIL_REGULAR_EXPRESSION "FAIL"
    LABELS "regression_gate")
if(NOT WIN32)
    add_test(NAME protopyc_exception_identity
        COMMAND ${CMAKE_SOURCE_DIR}/test/compiler/protopyc_exception_identity.sh
                $<TARGET_FILE:protopyc> $<TARGET_FILE:test_exception_identity>
                ${CMAKE_SOURCE_DIR}/test/exceptions/exception_identity_probe.py
                ${CMAKE_CURRENT_BINARY_DIR}/test/compiler/exception_identity)
    set_tests_properties(protopyc_exception_identity PROPERTIES
        TIMEOUT 600
        PASS_REGULAR_EXPRESSION "probe: [^\n]*\\.so.*exception_identity: PASS"
        FAIL_REGULAR_EXPRESSION "FAIL"
        LABELS "regression_gate")
endif()

# --- Errors of a C++ UMD module (protoCore 3.0.0 unified exceptions) ---------
# test/regression/umd_geometry_*.py import protoCore's example geometry module,
# a ModuleProvider in its own shared library (protoCore/examples/exceptions/),
# as `example.geometry`, and catch the error types it defines. protopy has no
# plug-in loader, so they run through protopy_umd_host (test/umd/), which links
# the library, registers its provider and appends it to the resolution chain.
# The library is protoCore's own target when protoCore is built here
# (add_subdirectory); with an installed protoCore name it with
# -DPROTOCORE_EXAMPLE_GEOMETRY_LIBRARY=<path> and
# -DPROTOCORE_EXAMPLE_GEOMETRY_INCLUDE_DIR=<dir holding geometry_module/>.
if(TARGET protocore_example_geometry)
    set(_protopython_geometry_lib protocore_example_geometry)
    set(_protopython_geometry_inc "${PROTOCORE_DIR}/examples/exceptions")
else()
    find_library(PROTOCORE_EXAMPLE_GEOMETRY_LIBRARY NAMES protocore_example_geometry
        PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../protoCore/build_release/examples/exceptions"
        NO_DEFAULT_PATH)
    find_path(PROTOCORE_EXAMPLE_GEOMETRY_INCLUDE_DIR geometry_module/GeometryModule.h
        PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../protoCore/examples/exceptions" NO_DEFAULT_PATH)
    set(_protopython_geometry_lib "${PROTOCORE_EXAMPLE_GEOMETRY_LIBRARY}")
    set(_protopython_geometry_inc "${PROTOCORE_EXAMPLE_GEOMETRY_INCLUDE_DIR}")
endif()
if(_protopython_geometry_lib AND _protopython_geometry_inc)
    add_executable(protopy_umd_host test/umd/protopy_umd_host.cpp)
    target_include_directories(protopy_umd_host PRIVATE ${_protopython_geometry_inc})
    target_link_libraries(protopy_umd_host PRIVATE protoPython protoCore ${_protopython_geometry_lib})
    target_compile_definitions(protopy_umd_host PRIVATE
        STDLIB_PATH="${CMAKE_SOURCE_DIR}/lib/python3.14")
    # Interpreted: the handlers, protoCore's errors through the factory, a
    # C++ std::exception as RuntimeError, foreign values in Python's protocols.
    add_test(NAME umd_geometry_errors
        COMMAND protopy_umd_host ${CMAKE_SOURCE_DIR}/test/regression/umd_geometry_errors.py)
    set_tests_properties(umd_geometry_errors PROPERTIES
        TIMEOUT 120
        PASS_REGULAR_EXPRESSION "umd_geometry_errors: PASS"
        FAIL_REGULAR_EXPRESSION "FAIL;Traceback"
        LABELS "regression_gate")
    # Unhandled: the module's error reaches the top level, which reports it by
    # its name and message and exits with status 1.
    add_test(NAME umd_geometry_unhandled
        COMMAND ${PROTOPYTHON_ASSERT_EXIT} 1 $<TARGET_FILE:protopy_umd_host>
                ${CMAKE_SOURCE_DIR}/test/regression/umd_geometry_unhandled.py)
    set_tests_properties(umd_geometry_unhandled PROPERTIES
        TIMEOUT 120
        PASS_REGULAR_EXPRESSION "NegativeRadius: circleArea: the radius -3.000000 is negative"
        FAIL_REGULAR_EXPRESSION "Expected exit code"
        LABELS "regression_gate")
    # Compiled: the same test module built by protopyc and imported.
    if(NOT WIN32)
        add_test(NAME protopyc_umd_geometry
            COMMAND ${CMAKE_SOURCE_DIR}/test/compiler/protopyc_umd_geometry.sh
                    $<TARGET_FILE:protopyc> $<TARGET_FILE:protopy_umd_host>
                    ${CMAKE_SOURCE_DIR}/test/regression/umd_geometry_errors.py
                    ${CMAKE_CURRENT_BINARY_DIR}/test/compiler/umd_geometry)
        set_tests_properties(protopyc_umd_geometry PROPERTIES
            TIMEOUT 600
            PASS_REGULAR_EXPRESSION "umd_geometry_errors: PASS"
            FAIL_REGULAR_EXPRESSION "FAIL"
            LABELS "regression_gate")
    endif()
else()
    message(STATUS "protoPython: protoCore's example geometry module was not found; "
                   "the umd_geometry tests (foreign error types) are not built")
endif()

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
# CMAKE_INSTALL_DEFAULT_COMPONENT_NAME, not CMAKE_INSTALL_COMPONENT: the latter
# is a variable the *generated* cmake_install.cmake sets from --component, and
# assigning it here had no effect at all.
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME "protoPython")

# COMPONENT is repeated for every artifact group. In install(TARGETS), once an
# artifact kind (RUNTIME/LIBRARY/ARCHIVE) is named, the keywords that follow
# bind to that group alone -- a single trailing COMPONENT applied to ARCHIVE
# only, which left protopy (RUNTIME) and libprotoPython (LIBRARY) in the
# default "Unspecified" component. `cmake --install --component protoPython`
# and every CPack package therefore shipped protopyc, the headers and the
# standard library, but neither the interpreter nor its shared library.
install(TARGETS protopy protoPython protopyc
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT protoPython
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT protoPython
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT protoPython
)

install(DIRECTORY include/protoPython
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT protoPython
)

install(DIRECTORY lib/python3.14
    DESTINATION ${PROTOPYTHON_STDLIB_INSTALL_PARENT}
    USE_SOURCE_PERMISSIONS
    COMPONENT protoPython
)

# Windows: a self-contained ZIP/installer, as protoST, protoScala, protoClojure
# and protoJS ship. protoCore's DLL is copied from the imported (or sibling)
# protoCore target, whatever it is named (protoCore-3.dll from protoCore 2.9.0
# on, protoCore.dll before), and the Microsoft C++ runtime DLLs (msvcp140.dll,
# vcruntime140.dll, ...) are deployed app-local through
# InstallRequiredSystemLibraries, so a machine without protoCore or the Visual
# C++ Redistributable runs the unpacked protopy.exe.
if(WIN32)
    if(TARGET protoCore)
        install(FILES "$<TARGET_FILE:protoCore>"
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT protoPython)
    endif()
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT protoPython)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_INSTALL_DEBUG_LIBRARIES ON)
        set(CMAKE_INSTALL_DEBUG_LIBRARIES_ONLY ON)
    endif()
    include(InstallRequiredSystemLibraries)
endif()

# --- CPack Configuration ---
set(CPACK_PACKAGE_NAME "protopython")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "protoPython - GIL-less Python 3.14 compatible runtime")
set(CPACK_PACKAGE_VENDOR "protoPython")
set(CPACK_PACKAGE_CONTACT "gamarino@gmail.com")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_COMPONENTS_ALL protoPython)

# Platform-specific configurations.
if(APPLE)
    # macOS drag-and-drop .dmg. UNVERIFIED: no macOS build host (D-I6).
    set(CPACK_GENERATOR "DragNDrop")
elseif(UNIX)
    # Enable only the generators whose tools are present: cpack aborts the whole
    # run when one of them is missing, which would take the TGZ down with it.
    # Same pattern as protoCore's own CPack block.
    set(CPACK_GENERATOR "TGZ")
    find_program(PROTOPYTHON_DPKG_EXECUTABLE dpkg)
    find_program(PROTOPYTHON_RPMBUILD_EXECUTABLE rpmbuild)
    if(PROTOPYTHON_DPKG_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "DEB")
        message(STATUS "CPack: DEB generator enabled (dpkg found)")
    else()
        message(STATUS "CPack: DEB generator disabled (dpkg not found)")
    endif()
    if(PROTOPYTHON_RPMBUILD_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "RPM")
        message(STATUS "CPack: RPM generator enabled (rpmbuild found)")
    else()
        message(STATUS "CPack: RPM generator disabled (rpmbuild not found)")
    endif()
    # Pinned rather than left to each generator's default casing: the DEB
    # generator lower-cases CPACK_PACKAGE_NAME and the RPM generator does not.
    set(CPACK_DEBIAN_PACKAGE_NAME "protopython")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "gamarino@gmail.com")
    set(CPACK_DEBIAN_PACKAGE_SECTION "interpreters")
    # protopy / libprotoPython link libprotoCore — declare the dependency on
    # protoCore's package (which provides libprotoCore), the same strategy
    # protoJS and protoST use. protoCore is never bundled. protoCore's major
    # version and its soname move together, so the dependency is bounded on
    # both sides: a 3.x protoCore would be ABI-incompatible with this build.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "protocore (>= ${PROTOCORE_MIN_VERSION_FULL}), protocore (<< ${PROTOCORE_NEXT_MAJOR}), libpcre2-8-0 (>= 10.34)")
    # SHLIBDEPS is meant to add a SONAME dependency (libprotoCore.so.N)
    # derived from the binary's own NEEDED entry, because the version range
    # above is not an ABI check on its own: a protoCore in range but built
    # with an older SOVERSION is accepted by dpkg and then fails at load
    # time (installers-sep2026 report, F4). Verified (in protoClojure, same
    # mechanism) that it does NOT add that dependency here: protoCore's own
    # .deb ships no shlibs/symbols file, so dpkg-shlibdeps has nothing to
    # emit for libprotoCore.so.3 and CPack's default --ignore-missing-info
    # drops it silently. Left on because it is harmless and adds real
    # transitive deps (libc6, libstdc++6, ...); closing F4 needs a shlibs or
    # symbols file in protoCore's own package, not this flag alone.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_RPM_PACKAGE_NAME "protoPython")
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Development/Languages")
    set(CPACK_RPM_PACKAGE_REQUIRES
        "protoCore >= ${PROTOCORE_MIN_VERSION_FULL}, protoCore < ${PROTOCORE_NEXT_MAJOR}")
elseif(WIN32)
    # The ZIP is built and run in CI (cross-platform.yml). NSIS only when
    # makensis is present: cpack aborts the whole run when a generator's tool
    # is missing, which would take the ZIP down with it. Both bundle protoCore's
    # DLL and the MSVC runtime (see the install rules above).
    set(CPACK_GENERATOR "ZIP")
    find_program(PROTOPYTHON_MAKENSIS_EXECUTABLE makensis)
    if(PROTOPYTHON_MAKENSIS_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "NSIS")
        message(STATUS "CPack: NSIS generator enabled (makensis found)")
    else()
        message(STATUS "CPack: NSIS generator disabled (makensis not found)")
    endif()
    set(CPACK_NSIS_HELP_LINK "https://github.com/gamarino/protoPython")
    set(CPACK_NSIS_PACKAGE_NAME "protoPython")
endif()

include(CPack)
