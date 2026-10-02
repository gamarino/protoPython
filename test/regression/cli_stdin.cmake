# cli_stdin.cmake: sys.stdin and input() read standard input as CPython does
# (a text layer over descriptor 0: UTF-8, newline="\n" on POSIX and universal
# newlines on Windows, with a binary `buffer`). Each case runs protopy with its
# standard input redirected from a file and compares the whole standard
# output, without a shell in between, so it runs the same way on Linux, macOS
# and Windows.
#
# usage: cmake -DPROTOPY=<protopy> -DWORK=<scratch directory> -P cli_stdin.cmake
if(NOT PROTOPY OR NOT WORK)
    message(FATAL_ERROR "usage: cmake -DPROTOPY=<protopy> -DWORK=<dir> -P cli_stdin.cmake")
endif()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(_failures 0)
# file(WRITE) writes in text mode on Windows ("\n" becomes "\r\n"), so there a
# "\r\n" in the input is written as "\n". Elsewhere the bytes are as given.
if(CMAKE_HOST_WIN32)
    set(CRLF "\n")
else()
    set(CRLF "\r\n")
endif()

# check(<name> <stdin text> <expected stdout> <protopy args>...)
# The arguments are a CMake list: Python code separates statements with "\n",
# never ";".
function(check name input expected)
    file(WRITE "${WORK}/${name}.in" "${input}")
    execute_process(COMMAND "${PROTOPY}" ${ARGN}
        INPUT_FILE "${WORK}/${name}.in"
        OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE rc)
    if(NOT rc STREQUAL "0" OR NOT out STREQUAL expected)
        message(SEND_ERROR "${name}: exit ${rc}\n--- expected ---\n${expected}\n--- got ---\n${out}\n--- stderr ---\n${err}")
        math(EXPR n "${_failures} + 1")
        set(_failures ${n} PARENT_SCOPE)
    else()
        message(STATUS "${name}: ok")
    endif()
endfunction()

# readline() keeps the line's "\n"; read() returns the rest.
check(readline_read "a\nb\n" "a\n\nb\n\n"
    -c "import sys\nprint(sys.stdin.readline())\nprint(sys.stdin.read())")
# Newlines as CPython's sys.stdin: on Windows universal newlines ("\r\n" and
# "\r" read as "\n"); elsewhere lines end at "\n" and nothing is translated.
# The last line may lack a newline.
if(CMAKE_HOST_WIN32)
    set(_lines "'x\\n'\n'y\\n'\n'z'\n")
else()
    set(_lines "'x\\r\\n'\n'y\\rz'\n")
endif()
check(iteration "x${CRLF}y\rz" "${_lines}"
    -c "import sys\nfor line in sys.stdin: print(repr(line))")
check(readlines "a\nb" "['a\\n', 'b']\n"
    -c "import sys\nprint(sys.stdin.readlines())")
check(readline_size "abcd\nef\n" "'ab' 'cd\\n' 'ef\\n'\n"
    -c "import sys\nr = sys.stdin.readline\nprint(repr(r(2)), repr(r(-1)), repr(r()))")
check(read_size "héllo\n" "'hé' 'llo\\n'\n"
    -c "import sys\nprint(repr(sys.stdin.read(2)), repr(sys.stdin.read()))")
# input() and sys.stdin share one buffer: lines are consumed in order. The
# prompt goes to standard output even when the input is not a terminal.
check(input_interleaved "one\ntwo\nthree\nfour\nfive\n" "p> ('one', 'two\\n', 'three', 'four\\n', 'five\\n')\n"
    -c "import sys\na = input()\nb = sys.stdin.readline()\nc = input('p> ')\nd = next(iter(sys.stdin))\ne = sys.stdin.read()\nprint((a, b, c, d, e))")
# End of file: readline() and read() return '', iteration stops, input()
# raises EOFError.
check(eof "" "'' '' [] EOFError\n"
    -c "import sys\nr = (repr(sys.stdin.readline()), repr(sys.stdin.read()), str(list(sys.stdin)))\ntry:\n    input()\nexcept EOFError:\n    print(*r, 'EOFError')")
check(input_last_line_without_newline "last" "'last'\n"
    -c "print(repr(input()))")
# The binary layer reads bytes.
check(buffer "hé${CRLF}" "b'h\\xc3\\xa9\\r\\n'\n"
    -c "import sys\nprint(sys.stdin.buffer.read())")
check(text_utf8 "hé\n" "3 True\n"
    -c "import sys\ns = sys.stdin.read()\nprint(len(s), s == 'h\\u00e9\\n')")
# The attributes of CPython's TextIOWrapper over descriptor 0.
check(attributes "" "0 True False utf-8 strict <stdin> r False False True <stdin> rb\n"
    -c "import sys\ns = sys.stdin\nprint(s.fileno(), s.readable(), s.writable(), s.encoding, s.errors, s.name, s.mode, s.isatty(), s.closed, s is sys.__stdin__, s.buffer.name, s.buffer.mode)")
# input() reads whatever sys.stdin is.
check(input_replaced_stdin "ignored\n" "from StringIO\n"
    -c "import io, sys\nsys.stdin = io.StringIO('from StringIO\\n')\nprint(input())")

# protopy with no target runs the program read from standard input when it is
# not a terminal, as CPython does (the REPL starts only on a terminal):
# sys.argv is [''], the program runs as __main__ with sys.path[0] '', and
# sys.stdin is at its end. "-" reads the program from standard input too and
# passes the remaining arguments to it.
check(program_from_stdin "import sys\nprint(repr(sys.stdin.read()), sys.argv, __name__, repr(sys.path[0]))\n"
    "'' [''] __main__ ''\n")
check(program_from_stdin_dash "import sys\nprint(sys.argv)\n" "['-', 'a', 'b']\n" - a b)
check(program_from_stdin_empty "" "")
check(program_from_stdin_comment "# only a comment\n" "")
# What that relies on: compile() takes an empty program in exec mode, and a
# SyntaxError carries the file name it was compiled under.
check(compile_empty_and_filename "" "True afile.py 1\n"
    -c "c = compile('', 'x', 'exec')\ntry:\n    compile('def f(:', 'afile.py', 'exec')\nexcept SyntaxError as e:\n    print(c is not None, e.filename, e.lineno)")
# An uncaught exception is reported against "<stdin>" with a failing status.
file(WRITE "${WORK}/program_from_stdin_error.in" "x = 1\nraise ValueError('boom')\n")
execute_process(COMMAND "${PROTOPY}" INPUT_FILE "${WORK}/program_from_stdin_error.in"
    OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE rc)
if(rc STREQUAL "0" OR NOT err MATCHES "File \"<stdin>\", line 2" OR NOT err MATCHES "ValueError: boom")
    message(SEND_ERROR "program_from_stdin_error: exit ${rc}\n--- stdout ---\n${out}\n--- stderr ---\n${err}")
    math(EXPR _failures "${_failures} + 1")
else()
    message(STATUS "program_from_stdin_error: ok")
endif()

# Threads reading sys.stdin concurrently from a pipe each get whole lines,
# and every line exactly once (test/regression/stdin_threads.py). The pipe is
# `cmake -E cat <file> | protopy`, so reads return pipe-sized chunks.
set(_lines_count 20000)
# The filler is the one stdin_threads.py expects.
string(REPEAT "x" 90 _filler)
# Written in blocks: appending every line to one CMake string is quadratic.
file(WRITE "${WORK}/stdin_threads.in" "")
math(EXPR _blocks "${_lines_count} / 500 - 1")
foreach(b RANGE ${_blocks})
    set(_text "")
    foreach(k RANGE 499)
        math(EXPR i "${b} * 500 + ${k}")
        string(APPEND _text "${i}:${_filler}\n")
    endforeach()
    file(APPEND "${WORK}/stdin_threads.in" "${_text}")
endforeach()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E cat "${WORK}/stdin_threads.in"
    COMMAND "${PROTOPY}" "${CMAKE_CURRENT_LIST_DIR}/stdin_threads.py" ${_lines_count}
    OUTPUT_VARIABLE out ERROR_VARIABLE err RESULTS_VARIABLE rcs)
if(NOT rcs STREQUAL "0;0" OR NOT out STREQUAL "stdin_threads: ${_lines_count} whole lines\n")
    message(SEND_ERROR "stdin_threads: exit ${rcs}\n--- got ---\n${out}\n--- stderr ---\n${err}")
    math(EXPR _failures "${_failures} + 1")
else()
    message(STATUS "stdin_threads: ok")
endif()

if(_failures GREATER 0)
    message(FATAL_ERROR "${_failures} standard input case(s) failed")
endif()
message(STATUS "cli_stdin: ok")
