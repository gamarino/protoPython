# os.execv / os.execve reject, with ValueError and without executing
# anything, the argument lists and environments CPython rejects.  An
# environment variable name with '=' or a NUL byte used to go through, and
# the exec replaced the running process (test_os's test_execve_invalid_env
# ended the whole test run with exit status 0).
import os
import sys

args = [sys.executable, "-c", "raise SystemExit(3)"]
for bad in ({"FRUIT\0VEGETABLE": "cabbage"}, {"FRUIT": "orange\0VEGETABLE=cabbage"},
            {"FRUIT=ORANGE": "lemon"}, {"": "x"}):
    newenv = dict(os.environ)
    newenv.update(bad)
    try:
        os.execve(args[0], args, newenv)
    except ValueError:
        pass
    else:
        raise AssertionError("execve accepted %r" % (bad,))

for bad_args in ((), [], ("",), [""]):
    for call in (lambda: os.execv(sys.executable, bad_args),
                 lambda: os.execve(sys.executable, bad_args, {})):
        try:
            call()
        except ValueError:
            pass
        else:
            raise AssertionError("exec accepted %r" % (bad_args,))
print("os_exec_argument_checks: ok")
