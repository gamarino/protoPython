# cli_sys_path.cmake: sys.path[0] and the import of the program's own modules,
# as CPython 3.14 sets them up:
#   protopy dir/script.py   sys.path[0] is the script's directory, absolute,
#                           with symbolic links resolved (POSIX)
#   protopy -m pkg.mod      sys.path[0] is the current directory, absolute
#   protopy -c code         sys.path[0] is '' (the current directory)
#   protopy -P ...          nothing is prepended (safe path; -I implies it)
# Each case runs protopy from a directory of its own and compares the whole
# standard output, without a shell in between.
#
# usage: cmake -DPROTOPY=<protopy> -DWORK=<scratch directory> -P cli_sys_path.cmake
if(NOT PROTOPY OR NOT WORK)
    message(FATAL_ERROR "usage: cmake -DPROTOPY=<protopy> -DWORK=<dir> -P cli_sys_path.cmake")
endif()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/elsewhere" "${WORK}/app/pkg")
set(_failures 0)

# The program's own modules: none of these names exists in the stdlib.
file(WRITE "${WORK}/app/sibling_of_script.py" "VALUE = 'sibling'\n")
file(WRITE "${WORK}/app/main_script.py" [=[
import os, sys
import sibling_of_script
here = os.path.dirname(os.path.realpath(__file__))
print(sibling_of_script.VALUE, sys.path[0] == here, os.path.isabs(sys.path[0]))
]=])
file(WRITE "${WORK}/app/pkg/__init__.py" "")
file(WRITE "${WORK}/app/pkg/mod.py" [=[
import os, sys
import cwd_module
print(__name__, cwd_module.VALUE, sys.path[0] == os.getcwd())
]=])
file(WRITE "${WORK}/app/cwd_module.py" "VALUE = 'cwd'\n")

# check(<name> <working directory> <expected stdout> <protopy args>...)
# The arguments are a CMake list: Python code separates statements with "\n",
# never ";".
function(check name dir expected)
    execute_process(COMMAND "${PROTOPY}" ${ARGN}
        WORKING_DIRECTORY "${dir}"
        OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE rc)
    if(NOT rc STREQUAL "0" OR NOT out STREQUAL expected)
        message(SEND_ERROR "${name}: exit ${rc}\n--- expected ---\n${expected}\n--- got ---\n${out}\n--- stderr ---\n${err}")
        math(EXPR n "${_failures} + 1")
        set(_failures ${n} PARENT_SCOPE)
    else()
        message(STATUS "${name}: ok")
    endif()
endfunction()

# A script run from another directory imports its sibling module.
check(script_relative "${WORK}/elsewhere" "sibling True True\n" ../app/main_script.py)
check(script_absolute "${WORK}/elsewhere" "sibling True True\n" "${WORK}/app/main_script.py")
# Through a symbolic link: the directory of the file it points to.
if(NOT CMAKE_HOST_WIN32)
    file(CREATE_LINK "${WORK}/app/main_script.py" "${WORK}/elsewhere/link_to_script.py" SYMBOLIC)
    check(script_symlink "${WORK}/elsewhere" "sibling True True\n" link_to_script.py)
endif()
# -m finds modules and packages in the current directory.
check(m_package_module "${WORK}/app" "__main__ cwd True\n" -m pkg.mod)
file(WRITE "${WORK}/app/cwd_module_main.py" "import cwd_module\nprint(cwd_module.VALUE)\n")
check(m_module "${WORK}/app" "cwd\n" -m cwd_module_main)
# -c: '' first, and the current directory's modules import.
check(c_import "${WORK}/app" "'' cwd\n"
    -c "import sys, cwd_module\nprint(repr(sys.path[0]), cwd_module.VALUE)")
# The standard library is not first.
check(c_stdlib_not_first "${WORK}/app" "True\n"
    -c "import sys, os\nprint(not os.path.isfile(os.path.join(sys.path[0], 'os.py')))")
# -P: nothing prepended, so the current directory's modules do not import.
check(safe_path "${WORK}/app" "False ModuleNotFoundError\n"
    -P -c "import sys\ntry:\n    import cwd_module\nexcept ImportError as e:\n    print(sys.path[0] in ('', '.'), type(e).__name__)")

if(_failures GREATER 0)
    message(FATAL_ERROR "${_failures} sys.path case(s) failed")
endif()
message(STATUS "cli_sys_path: ok")
