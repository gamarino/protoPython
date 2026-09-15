# platform parses sys.version, which follows CPython's
# "X.Y.Z (build, date) [compiler]" layout.
import platform
import sys

expected = "%d.%d.%d" % tuple(sys.version_info[:3])
assert sys.version.split()[0] == expected, sys.version
assert platform.python_version() == expected
assert platform.python_version_tuple() == tuple(expected.split("."))

build = platform.python_build()
assert isinstance(build, tuple) and len(build) == 2 and build[0], build
assert platform.python_compiler(), sys.version
assert isinstance(platform.python_implementation(), str)

print("platform_version: ok")
