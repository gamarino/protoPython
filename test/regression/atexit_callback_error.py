# An atexit handler that raises is reported as CPython reports it, through
# sys.unraisablehook ("Exception ignored in atexit callback <function ...>:"
# followed by the traceback), and the remaining handlers still run with the
# positional and keyword arguments they were registered with.  CTest matches
# the whole report (see CMakeLists.txt); every line goes to stderr so the
# order is fixed.
import atexit
import sys


def failing_handler():
    raise ValueError("boom")


def later_handler(a, b=2):
    sys.stderr.write("later_handler %r %r\n" % (a, b))
    sys.stderr.flush()


# Handlers run last-registered first: failing_handler, then later_handler.
atexit.register(later_handler, 1, b=3)
atexit.register(failing_handler)
sys.stderr.write("atexit_callback_error: registered\n")
sys.stderr.flush()
