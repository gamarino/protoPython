# platform.python_implementation() names the running implementation
# consistently with sys.implementation.name.
import platform
import sys

names = {"cpython": "CPython", "pypy": "PyPy", "protopython": "protoPython"}
assert sys.implementation.name in names, sys.implementation.name
assert platform.python_implementation() == names[sys.implementation.name], (
    platform.python_implementation(), sys.implementation.name)

# The rest of the sys.version parse still works.
assert platform.python_version() == "%d.%d.%d" % sys.version_info[:3], platform.python_version()
assert platform.python_version_tuple() == tuple(str(x) for x in sys.version_info[:3])
build = platform.python_build()
assert len(build) == 2 and all(isinstance(x, str) for x in build), build
assert platform.python_compiler(), platform.python_compiler()

print("platform_implementation: ok")
