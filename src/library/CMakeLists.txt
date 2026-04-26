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
    JsonModule.cpp
    LoggingModule.cpp
    MathModule.cpp
    OperatorModule.cpp
    ReModule.cpp
    OsModule.cpp
    OsPathModule.cpp
    PathlibModule.cpp
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
    MarshalModule.cpp
    ImpModule.cpp
    WarningsModule.cpp
    OpcodeModule.cpp
    StringModule.cpp
    BinasciiModule.cpp
    DatetimeModule.cpp
    PosixSubprocessModule.cpp
    SelectModule.cpp
    BisectModule.cpp
    HeapqModule.cpp
    FaulthandlerModule.cpp
)

target_include_directories(protoPython PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

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
target_compile_options(protoPython PRIVATE -g)

# Same intra-DSO PLT-elision as protoCore: profile of nqueens showed every
# recursive ContextScope teardown trampolining through PLT for callees that
# live in the same shared library.  -Bsymbolic-functions keeps the symbol
# exported but resolves intra-DSO references at link time.
if(NOT WIN32 AND NOT APPLE)
    target_link_options(protoPython PRIVATE "-Wl,-Bsymbolic-functions")
endif()
