# A file object that is never closed releases its descriptor when the
# collector reclaims it.
#
# CPython closes such a file as soon as its last reference goes. protoPython
# has no reference counts, so a file nobody closes stays open until it is
# collected; before this, its descriptor was never released at all. protoCore
# collects only under a heap limit, so this test runs with
# PROTOCORE_HEAP_LIMIT_CELLS (CMakeLists.txt) and allocates until a collection
# has happened. `with` and close() release the descriptor at once
# (unclosed_files_directory_cleanup.py covers removing their directories).
#
# NOT RUN ON WINDOWS (exit 77, reported by CTest as skipped): there, a
# heap-limited collection under this workload crashes the process or corrupts
# live strings some of the time, with or without this change -- the same
# script crashed 3 of 20 runs built from main (CI run 36974436607), and with a
# finalizer that does nothing (run 36973738152). It is a defect of collection
# on Windows, not of descriptor release; see docs/INSTALLATION.md, "Windows
# (MSVC)".
import os
import sys
import tempfile

if sys.platform == "win32":
    print("file_close_on_collect: SKIPPED on Windows: heap-limited collection is unsafe there "
          "(CI run 36974436607)")
    sys.exit(77)


def leaked_descriptor(path):
    f = open(path, "w")
    f.write("x")
    return f.fileno()


def descriptor_is_open(fd):
    try:
        os.fstat(fd)
    except OSError:
        return False
    return True


d = tempfile.mkdtemp()
fds = [leaked_descriptor(os.path.join(d, "g%d.txt" % i)) for i in range(8)]
if sys.implementation.name == "cpython":
    still_open = [fd for fd in fds if descriptor_is_open(fd)]
else:
    for attempt in range(500):
        junk = [(i, i + 1) for i in range(2000)]
        still_open = [fd for fd in fds if descriptor_is_open(fd)]
        if not still_open:
            break
assert not still_open, "descriptors of unreachable files still open: %r" % still_open

# The data written through an unclosed file reached the file.
for i in range(8):
    with open(os.path.join(d, "g%d.txt" % i)) as r:
        assert r.read() == "x"
    os.remove(os.path.join(d, "g%d.txt" % i))
os.rmdir(d)
print("file_close_on_collect: ok")
