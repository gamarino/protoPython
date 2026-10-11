# compare_output.cmake: runs a protopy program and requires its standard output
# to equal an expected file (CPython's output on the same source), without a
# shell in between, so it runs the same way on Linux, macOS and Windows.
# Environment variables for the run are passed as VAR=VALUE items of ENV
# (a CMake list).
#
# usage: cmake -DPROTOPY=<protopy> -DSCRIPT=<program.py> -DEXPECTED=<file>
#              [-DENV=VAR=VALUE;...] -P compare_output.cmake
if(NOT PROTOPY OR NOT SCRIPT OR NOT EXPECTED)
    message(FATAL_ERROR "usage: cmake -DPROTOPY=<protopy> -DSCRIPT=<py> -DEXPECTED=<file> -P compare_output.cmake")
endif()
set(_cmd "${PROTOPY}" "${SCRIPT}")
if(ENV)
    set(_cmd ${CMAKE_COMMAND} -E env ${ENV} ${_cmd})
endif()
execute_process(COMMAND ${_cmd}
    OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE rc)
file(READ "${EXPECTED}" expected)
# A text-mode run on Windows writes "\r\n".
string(REPLACE "\r\n" "\n" out "${out}")
string(REPLACE "\r\n" "\n" expected "${expected}")
if(NOT rc STREQUAL "0" OR NOT out STREQUAL expected)
    message(FATAL_ERROR "exit ${rc}\n--- expected ---\n${expected}\n--- got ---\n${out}\n--- stderr ---\n${err}")
endif()
message(STATUS "output matches ${EXPECTED}")
