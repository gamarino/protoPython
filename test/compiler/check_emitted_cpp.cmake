# The C++ that `protopyc --emit-cpp` writes uses fixed-width integer types and
# portable overflow checks: no `long` (32 bits on Windows, 64 elsewhere) and no
# GCC/Clang builtins. Then it compiles, with the compiler that built protoPython,
# to an object file (MSVC included: this is what CI verifies on Windows).
#
#   cmake -DPROTOPYC=<protopyc> -DSOURCE=<probe.py> -DWORK_DIR=<dir>
#         -DCXX=<compiler> -DINCLUDE_DIRS=<dir;dir> [-DMSVC=1] -P check_emitted_cpp.cmake
foreach(var PROTOPYC SOURCE WORK_DIR CXX INCLUDE_DIRS)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "check_emitted_cpp: ${var} is not set")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
get_filename_component(_name "${SOURCE}" NAME_WE)
file(COPY "${SOURCE}" DESTINATION "${WORK_DIR}")
execute_process(COMMAND "${PROTOPYC}" "${_name}.py" --emit-cpp
    WORKING_DIRECTORY "${WORK_DIR}" RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "protopyc --emit-cpp failed (${_rc}):\n${_out}${_err}")
endif()
set(_cpp "${WORK_DIR}/${_name}.cpp")
if(NOT EXISTS "${_cpp}")
    message(FATAL_ERROR "protopyc wrote no ${_cpp}")
endif()

# Word `long` and compiler builtins, outside comments.
file(STRINGS "${_cpp}" _lines)
set(_bad "")
set(_n 0)
foreach(_line IN LISTS _lines)
    math(EXPR _n "${_n} + 1")
    string(REGEX REPLACE "//.*$" "" _code "${_line}")
    if(_code MATCHES "(^|[^A-Za-z0-9_])long([^A-Za-z0-9_]|$)" OR _code MATCHES "__builtin_")
        string(APPEND _bad "  ${_n}: ${_line}\n")
    endif()
endforeach()
if(_bad)
    message(FATAL_ERROR "the emitted C++ uses `long` or a compiler builtin:\n${_bad}")
endif()

# Compile it to an object file.
set(_cmd "${CXX}")
if(MSVC)
    list(APPEND _cmd /nologo /std:c++20 /EHsc /utf-8 /DNOMINMAX /c "${_cpp}" "/Fo${WORK_DIR}/${_name}.obj")
    foreach(_dir IN LISTS INCLUDE_DIRS)
        list(APPEND _cmd "/I${_dir}")
    endforeach()
else()
    list(APPEND _cmd -std=c++20 -fPIC -c "${_cpp}" -o "${WORK_DIR}/${_name}.o")
    foreach(_dir IN LISTS INCLUDE_DIRS)
        list(APPEND _cmd "-I${_dir}")
    endforeach()
endif()
execute_process(COMMAND ${_cmd} WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "the emitted C++ does not compile (${_rc}):\n${_out}${_err}")
endif()
message(STATUS "protopyc emitted C++: no `long`, no builtins, compiles")
