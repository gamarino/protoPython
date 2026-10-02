# A file object that is never closed releases its descriptor when the
# collector reclaims it, and gc.collect() runs the collector.
#
# CPython closes such a file as soon as its last reference goes. protoPython
# has no reference counts, so a file nobody closes stays open until a
# collection; before this, its descriptor was never released at all, and on
# Windows a directory holding such files could not be removed. `with` and
# close() release the descriptor immediately, as always.
import gc
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
fds = [leaked_descriptor(os.path.join(d, "g%d.txt" % i)) for i in range(8)]
for attempt in range(5):
    gc.collect()
    still_open = [fd for fd in fds if descriptor_is_open(fd)]
    if not still_open:
        break
assert not still_open, "descriptors of unreachable files still open: %r" % still_open

# The data written through an unclosed file reached the file once collected.
for i in range(8):
    with open(os.path.join(d, "g%d.txt" % i)) as r:
        assert r.read() == "x"

# A directory whose files were written and never closed can be removed.
for i in range(20):
    write_implicitly(os.path.join(d, "f%d.txt" % i), "data %d" % i)
shutil.rmtree(d)
assert not os.path.exists(d)

# TemporaryDirectory cleans up after files opened and dropped in its scope.
with tempfile.TemporaryDirectory() as td:
    for i in range(10):
        write_implicitly(os.path.join(td, "%d.txt" % i), "v%d" % i)
    contents = [open(os.path.join(td, "%d.txt" % i)).read() for i in range(10)]
    assert contents == ["v%d" % i for i in range(10)], contents
    os.mkdir(os.path.join(td, "sub"))
    write_implicitly(os.path.join(td, "sub", "deep.txt"), "deep")
assert not os.path.exists(td), "TemporaryDirectory left " + td

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
