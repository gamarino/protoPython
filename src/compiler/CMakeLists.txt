add_executable(protopyc
    ProtopycMain.cpp
    CppGenerator.cpp
)

target_include_directories(protopyc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(protopyc PRIVATE
    protoPython
    protoCore
)

install(TARGETS protopyc
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT protoPython
)
