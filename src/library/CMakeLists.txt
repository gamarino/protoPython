add_library(protoPython SHARED
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
    NativeModuleProvider.cpp
    SysModule.cpp
    TimeModule.cpp
    ThreadModule.cpp
    BuiltinsModule.cpp
    IOModule.cpp
    CollectionsModule.cpp
    ExceptionsModule.cpp
)

target_include_directories(protoPython PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

# Target the parent directory for protoCore headers
# (The root CMakeLists.txt already handles basic include dirs, but let's be explicit if needed)
target_link_libraries(protoPython PUBLIC
    protoCore
)
