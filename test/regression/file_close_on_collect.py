# A file object that is never closed releases its descriptor when the
# collector reclaims it.
#
# CPython closes such a file as soon as its last reference goes. protoPython
# has no reference counts, so a file nobody closes stays open until it is
# collected; before this, its descriptor was never released at all. protoCore
# collects only under a heap limit, so this test runs with
# PROTOCORE_HEAP_LIMIT_CELLS (CMakeLists.txt) and allocates until a collection
# has happened. `with` and close() release the descriptor immediately, as
# always; directories holding files that were never closed can be removed
# (FILE_SHARE_DELETE and POSIX delete semantics on Windows).
import os
import shutil
import tempfile


def write_implicitly(path, text):
    open(path, "w").write(text)  # never closed explicitly


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
print("file_close_on_collect: leaking descriptors in", d)
fds = [leaked_descriptor(os.path.join(d, "g%d.txt" % i)) for i in range(8)]
import sys
if sys.implementation.name == "cpython":
    still_open = [fd for fd in fds if descriptor_is_open(fd)]
else:
    for attempt in range(500):
        junk = [(i, i + 1) for i in range(2000)]
        still_open = [fd for fd in fds if descriptor_is_open(fd)]
        if not still_open:
            break
assert not still_open, "descriptors of unreachable files still open: %r" % still_open

print("file_close_on_collect: descriptors released")
# The data written through an unclosed file reached the file once collected.
for i in range(8):
    with open(os.path.join(d, "g%d.txt" % i)) as r:
        assert r.read() == "x"

print("file_close_on_collect: removing the directory")
# A directory whose files were written and never closed can be removed.
for i in range(20):
    write_implicitly(os.path.join(d, "f%d.txt" % i), "data %d" % i)
shutil.rmtree(d)
assert not os.path.exists(d)

print("file_close_on_collect: TemporaryDirectory")
# TemporaryDirectory cleans up after files opened and dropped in its scope.
# Each step is announced, and an OSError names its errno and filename, so a
# failure on a platform this cannot be run on locally is located by its log.
step = "start"
try:
    with tempfile.TemporaryDirectory() as td:
        for i in range(10):
            step = "write %d" % i
            write_implicitly(os.path.join(td, "%d.txt" % i), "v%d" % i)
        step = "read back"
        contents = [open(os.path.join(td, "%d.txt" % i)).read() for i in range(10)]
        assert contents == ["v%d" % i for i in range(10)], contents
        step = "mkdir sub"
        os.mkdir(os.path.join(td, "sub"))
        step = "write sub/deep.txt"
        write_implicitly(os.path.join(td, "sub", "deep.txt"), "deep")
        step = "cleanup"
except OSError as e:
    print("file_close_on_collect: %s at step %r: errno %r, filename %r"
          % (type(e).__name__, step, e.errno, e.filename))
    # Locate a failure of the constructor itself.
    for name, probe in (("gettempdir", tempfile.gettempdir), ("mkdtemp", tempfile.mkdtemp),
                        ("getcwd", os.getcwd)):
        try:
            print("  %s -> %r" % (name, probe()))
        except OSError as inner:
            print("  %s raised %s errno %r filename %r" % (name, type(inner).__name__, inner.errno, inner.filename))
    raise
assert not os.path.exists(td), "TemporaryDirectory left " + td

print("file_close_on_collect: explicit close")
# Explicit close and `with` release the descriptor at once.
p = tempfile.mktemp()
f = open(p, "w")
fd = f.fileno()
f.close()
assert not descriptor_is_open(fd)
with open(p, "w") as g:
    fd = g.fileno()
assert not descriptor_is_open(fd)
os.remove(p)

print("file_close_on_collect: ok")
