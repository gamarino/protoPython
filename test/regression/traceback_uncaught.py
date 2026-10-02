"""An uncaught exception prints CPython's traceback: one File line per frame
(file, line, function), the source line under it, the exception last.
Run by CTest, which matches the expected output (see CMakeLists.txt)."""


def inner(n):
    return n / 0


def outer():
    return inner(1) + 1


outer()
