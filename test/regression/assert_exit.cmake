# assert_exit.cmake — Windows counterpart of assert_exit.sh: runs a command and
# checks its exit status, without a shell between CTest and the program (Git
# Bash's own command-line parsing drops the single quotes of `protopy -c` code).
#
# usage: cmake -P assert_exit.cmake -- <expected status> <command> [args...]
set(_args "")
set(_seen_separator FALSE)
math(EXPR _last "${CMAKE_ARGC} - 1")
foreach(_i RANGE 1 ${_last})
    if(_seen_separator)
        list(APPEND _args "${CMAKE_ARGV${_i}}")
    elseif(CMAKE_ARGV${_i} STREQUAL "--")
        set(_seen_separator TRUE)
    endif()
endforeach()
list(POP_FRONT _args _expected)
execute_process(COMMAND ${_args} RESULT_VARIABLE _status)
if(NOT _status STREQUAL _expected)
    message(FATAL_ERROR "Expected exit code ${_expected} but got ${_status}")
endif()
