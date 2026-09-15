# HPy extension modules load through HPyModuleProvider: the module keeps its
# name, file and functions, the functions receive and return Python values, and
# a shared library without an HPyInit_<name> entry point is not a module.
#
# Run with the directory holding math_hpy.hpy.so and hpy_no_init.hpy.so on the
# module search path (protopy -p <dir>).
import math_hpy
import sys

assert math_hpy.__name__ == "math_hpy", math_hpy.__name__
assert math_hpy.__file__.endswith("math_hpy.hpy.so"), math_hpy.__file__
assert callable(math_hpy.add)
assert math_hpy.add(2, 3) == 5
assert math_hpy.add(2.5, 1) == 3.5
assert math_hpy.add("ab", "cd") == "abcd"
assert sys.modules["math_hpy"] is math_hpy
assert __import__("math_hpy") is math_hpy

try:
    import hpy_no_init
except ImportError:
    pass
else:
    raise AssertionError("a library without HPyInit_hpy_no_init was imported")

print("HPy module basics OK")
