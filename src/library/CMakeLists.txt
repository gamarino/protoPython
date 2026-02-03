add_library(protoPython SHARED
    PythonEnvironment.cpp
    PythonModuleProvider.cpp
    NativeModuleProvider.cpp
    SysModule.cpp
    BuiltinsModule.cpp
)

target_include_directories(protoPython PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

# Target the parent directory for protoCore headers
# (The root CMakeLists.txt already handles basic include dirs, but let's be explicit if needed)
target_link_libraries(protoPython PUBLIC
    protoCore
)
