# sys.version carries the build date of the runtime ("Mon DD YYYY"), not a
# fixed string. When the build passes its date as the first argument, the
# date must match it exactly.
import platform
import re
import sys

match = re.match(r"^\d+\.\d+\.\d+\S* \(([^,]+), ([A-Z][a-z]{2} [ \d]\d \d{4})(?:, [\d:]+)?\) \[[^\]]+\]$",
                 sys.version)
assert match is not None, sys.version
builddate = match.group(2)
assert platform.python_build()[1].startswith(builddate), (platform.python_build(), builddate)

if len(sys.argv) > 1:
    assert builddate == sys.argv[1], (builddate, sys.argv[1])

print("sys_version_build_date: ok")
