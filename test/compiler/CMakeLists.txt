# run_module: minimal dlopen harness used by the benchmark suite to
# load a protopyc-generated .so and invoke proto_module_init.  Lives
# under test/compiler/ because it is a development helper, not a
# distributable; it is built so a normal `cmake --build` keeps it in
# sync with the current libprotoPython / libprotoCore ABI (the manual
# build path that preceded this CMakeLists drifted in May 2026 and
# silently broke the protopyc benchmark column).
add_executable(run_module run_module.cpp)

target_include_directories(run_module PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
)

target_link_libraries(run_module PRIVATE
    protoPython
    protoCore
    dl
)
