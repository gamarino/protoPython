# tempfile's files and directories: create, write, read back, close -> gone.
#
# NamedTemporaryFile and TemporaryFile open their file with mode "w+b" through
# an `opener` (on Windows with os.O_TEMPORARY, so that the system deletes the
# file when it is closed). Read-write modes and openers used to be unsupported
# by open(), and os.O_TEMPORARY did not exist on Windows.
import os
import sys
import tempfile

if os.name == "nt":
    for name in ("O_BINARY", "O_TEXT", "O_NOINHERIT", "O_TEMPORARY",
                 "O_SHORT_LIVED", "O_SEQUENTIAL", "O_RANDOM"):
        assert isinstance(getattr(os, name, None), int), "os.%s is missing" % name

# NamedTemporaryFile, binary (the default "w+b").
f = tempfile.NamedTemporaryFile()
name = f.name
assert isinstance(name, str) and os.path.exists(name), name
assert f.write(b"hello\nworld") == 11
f.flush()
f.seek(0)
assert f.read() == b"hello\nworld"
f.seek(6)
assert f.read(3) == b"wor"
assert f.tell() == 9
f.close()
assert not os.path.exists(name), "NamedTemporaryFile survived close()"

# As a context manager, in text mode.
with tempfile.NamedTemporaryFile("w+", encoding="utf-8", suffix=".txt") as g:
    gname = g.name
    assert gname.endswith(".txt")
    g.write("línea 1\nlínea 2\n")
    g.seek(0)
    assert g.read() == "línea 1\nlínea 2\n"
    g.seek(0)
    assert g.readline() == "línea 1\n"
    assert os.path.exists(gname)
assert not os.path.exists(gname), "NamedTemporaryFile survived the with block"

# delete=False keeps the file after close; another open() reads it.
h = tempfile.NamedTemporaryFile(delete=False, suffix=".bin")
h.write(b"\x00\x01\x02")
h.close()
assert os.path.exists(h.name)
with open(h.name, "rb") as r:
    assert r.read() == b"\x00\x01\x02"
os.unlink(h.name)
assert not os.path.exists(h.name)

# delete_on_close=False: the file outlives close() and goes at the end of the
# with block (on Windows this is the variant without O_TEMPORARY).
with tempfile.NamedTemporaryFile(delete_on_close=False) as k:
    k.write(b"kept")
    k.close()
    with open(k.name, "rb") as r:
        assert r.read() == b"kept"
assert not os.path.exists(k.name)

# TemporaryFile: anonymous (POSIX) or deleted on close (Windows).
t = tempfile.TemporaryFile()
t.write(b"abc")
t.seek(0)
assert t.read() == b"abc"
t.seek(1)
assert t.read(1) == b"b"
assert t.tell() == 2
t.close()
with tempfile.TemporaryFile("w+") as tt:
    tt.write("text")
    tt.seek(0)
    assert tt.read() == "text"

# TemporaryDirectory with files and a subdirectory in it.
with tempfile.TemporaryDirectory() as d:
    p = os.path.join(d, "x.txt")
    with open(p, "w") as w:
        w.write("data")
    with open(p) as r:
        assert r.read() == "data"
    os.mkdir(os.path.join(d, "sub"))
    with open(os.path.join(d, "sub", "y.bin"), "wb") as w:
        w.write(b"y")
    assert sorted(os.listdir(d)) == ["sub", "x.txt"]
assert not os.path.exists(d), "TemporaryDirectory survived cleanup"

# mkstemp / mkdtemp.
fd, path = tempfile.mkstemp()
os.write(fd, b"z")
os.close(fd)
assert os.path.getsize(path) == 1
os.unlink(path)
dd = tempfile.mkdtemp()
assert os.path.isdir(dd)
os.rmdir(dd)

print("tempfile_roundtrip: ok")
