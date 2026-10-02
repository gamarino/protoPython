# Objects held only by running frames survive heap-limited collections.
#
# A context that protoPython created with a `previous` other than the thread's
# current context hid the frames in between from protoCore's root scan, and
# left an outer context current after it was destroyed while inner frames were
# still running. Objects those frames held -- here the module-level value
# tempfile.tempdir and the strings built while files are opened -- were then
# swept: the process crashed, or a live string read back as NUL bytes. On
# Windows this script failed about one run in ten (CI run 36987826465,
# baseline: 9 of 100); test/library/TestContextChain.cpp checks the chain
# itself deterministically. Runs under PROTOCORE_HEAP_LIMIT_CELLS
# (CMakeLists.txt), the only configuration in which protoCore collects.
import os
import shutil
import tempfile

d = tempfile.mkdtemp()
saved = tempfile.tempdir


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


def write_implicitly(path, text):
    open(path, "w").write(text)


fds = [leaked_descriptor(os.path.join(d, "g%d.txt" % i)) for i in range(8)]
for attempt in range(40):
    junk = [(i, i + 1) for i in range(2000)]
    still_open = [fd for fd in fds if descriptor_is_open(fd)]
first = tempfile.tempdir == saved
for i in range(20):
    write_implicitly(os.path.join(d, "f%d.txt" % i), "data %d" % i)
for attempt in range(40):
    junk = [(i, i + 1) for i in range(2000)]
second = tempfile.tempdir == saved
for i in range(20):
    with open(os.path.join(d, "f%d.txt" % i)) as r:
        assert r.read() == "data %d" % i, "file %d corrupted" % i
assert first and second, "a module-level string changed: %r" % (tempfile.tempdir,)
assert len(junk) == 2000 and junk[1999] == (1999, 2000)
shutil.rmtree(d, ignore_errors=True)
print("heap_limit_live_frames: ok, %d allocation rounds" % (2 * 40))
