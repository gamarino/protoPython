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
# Windows runs it too. It was skipped there while a heap-limited collection
# under this workload crashed the process or corrupted live strings in some
# runs; the cause was contexts created with a `previous` other than the
# thread's current context, which hid live contexts from the collector
# (chainParent in include/protoPython/MemoryManager.hpp).
import os
import sys
import tempfile


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
