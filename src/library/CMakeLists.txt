add_library(protoPython SHARED
    HPyContext.cpp
    BytecodeLoader.cpp
    Compiler.cpp
    ExecutionEngine.cpp
    ThreadingStrategy.cpp
    BasicBlockAnalysis.cpp
    Parser.cpp
    Tokenizer.cpp
    FunctoolsModule.cpp
    ItertoolsModule.cpp
    LoggingModule.cpp
    MathModule.cpp
    OperatorModule.cpp
    ReModule.cpp
    OsModule.cpp
    OsPathModule.cpp
    CollectionsAbcModule.cpp
    AtexitModule.cpp
    PythonEnvironment.cpp
    PythonModuleProvider.cpp
    CompiledModuleProvider.cpp
    NativeModuleProvider.cpp
    HPyModuleProvider.cpp
    SysModule.cpp
    ThreadModule.cpp
    SignalModule.cpp
    TimeModule.cpp
    BuiltinsModule.cpp
    IOModule.cpp
    CollectionsModule.cpp
    CodecsModule.cpp
    ExceptionsModule.cpp
    WeakrefModule.cpp
    AstModule.cpp
    ErrnoModule.cpp
    StatModule.cpp
    StructModule.cpp
    StructSequence.cpp
    MarshalModule.cpp
    ImpModule.cpp
    WarningsModule.cpp
    OpcodeModule.cpp
    StringModule.cpp
    BinasciiModule.cpp
    DatetimeModule.cpp
    PosixSubprocessModule.cpp
    FcntlModule.cpp
    SelectModule.cpp
    BisectModule.cpp
    FaulthandlerModule.cpp
)
protopython_warnings(protoPython)

target_include_directories(protoPython PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
# Generated protoPython/Version.h (see the top-level CMakeLists.txt).
target_include_directories(protoPython PRIVATE ${PROTOPYTHON_GENERATED_INCLUDE_DIR})

# Target the parent directory for protoCore headers
# (The root CMakeLists.txt already handles basic include dirs, but let's be explicit if needed)
target_link_libraries(protoPython PUBLIC
    protoCore
)

set_target_properties(protoPython PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)

# Honour the global build type.  A previous `target_compile_options(... -O0 -g)`
# unconditionally forced libprotoPython to compile at -O0, even in Release
# builds — the `-O0` simply landed after the global `-O2` from CMAKE_CXX_FLAGS
# and won.  Profiling missed this because the dispatcher is a single huge
# function and looked plausibly slow at any optimisation level.
#
# Now we apply -g unconditionally (debuginfo is useful for Release crash
# reports too) and let CMAKE_CXX_FLAGS_<CONFIG> control the optimisation
# level.  Override per-config below if a finer-grained tweak is needed.
target_compile_options(protoPython PRIVATE ${PROTOPYTHON_DEBUG_INFO_FLAG})

if(WIN32)
    # The POSIX calls of the native modules, mapped onto Win32 (PosixCompat.h).
    # and the part of _winapi the standard library imports on Windows.
    target_sources(protoPython PRIVATE PosixCompat.cpp WinapiModule.cpp)
    # _posixsubprocess and fcntl exist only on POSIX systems (as in CPython):
    # their sources stay in the target but are not compiled, and
    # PythonEnvironment.cpp does not register them.
    set_source_files_properties(PosixSubprocessModule.cpp FcntlModule.cpp
        PROPERTIES HEADER_FILE_ONLY ON)
    # No __declspec annotations in the sources: export every symbol, as ELF does.
    set_target_properties(protoPython PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    # Windows finds a DLL next to the program that loads it: copy protoCore's DLL
    # (named by the imported target: protoCore-3.dll from 2.9.0 on, protoCore.dll
    # before) into the build's bin/ so protopy and the tests run in place. A target the
    # library depends on, not a post-build step: the tests are run to list their
    # cases as soon as they are linked.
    add_custom_target(protopython_runtime_dlls
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:protoCore>"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        VERBATIM)
    add_dependencies(protoPython protopython_runtime_dlls)
endif()

# Step #3 (2026-06-15): TLS model. Every thread_local declared inside this
# shared library defaults to the `global-dynamic` model, which means every
# access calls __tls_get_addr — a libc helper that resolves the variable
# at runtime. perf showed __tls_get_addr at 14.54 % of fib(25) wall-clock
# after step #4. Switching to `initial-exec` makes each access a single
# `%fs:offset` load (no helper call), which is correct because protopy
# links libprotoPython.so at program load — never via dlopen after start.
#
# If a future deployment ever needs to dlopen libprotoPython at runtime
# (REPL extensions, embedding scenarios), revert this flag — initial-exec
# requires the library to be in the loader's initial set.
if(NOT WIN32 AND NOT APPLE)
    target_compile_options(protoPython PRIVATE -ftls-model=initial-exec)
endif()

# Same intra-DSO PLT-elision as protoCore: profile of nqueens showed every
# recursive ContextScope teardown trampolining through PLT for callees that
# live in the same shared library.  -Bsymbolic-functions keeps the symbol
# exported but resolves intra-DSO references at link time.
if(NOT WIN32 AND NOT APPLE)
    target_link_options(protoPython PRIVATE "-Wl,-Bsymbolic-functions")
endif()
