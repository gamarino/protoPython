# seek / tell / seekable / readable / writable on every kind of file object,
# as in CPython. Read-mode files from open() are read whole when opened and
# keep no descriptor; their text-mode tell() cookie is the byte offset in the
# decoded, newline-translated UTF-8 text (CPython's is the byte offset in the
# file plus decoder state; both are opaque and only good for seek()). Without
# seek/tell, tokenize.open could not rewind a source file, so
# traceback.format_exc() showed no source lines.
import io
import os
import sys
import tempfile
import traceback

d = tempfile.mkdtemp()
path = os.path.join(d, "data.txt")
with open(path, "w") as f:
    f.write("héllo\nworld\n")          # 13 bytes in UTF-8
size = os.path.getsize(path)
assert size == 13, size


def raises(exc, fn, *args):
    try:
        fn(*args)
    except exc as e:
        return e
    raise AssertionError("%s%r did not raise %s" % (fn.__name__, args, exc.__name__))


# Text read mode.
f = open(path)
assert (f.readable(), f.writable(), f.seekable()) == (True, False, True)
assert f.tell() == 0
assert f.read(2) == "hé", "text read(n) counts characters"
assert f.readline() == "llo\n"
cookie = f.tell()
assert f.read() == "world\n"
assert f.seek(cookie) == cookie
assert f.readline() == "world\n"
assert f.seek(0) == 0 and f.read(1) == "h"
assert f.seek(0, 1) == f.tell()
assert f.seek(0, 2) == size and f.read() == ""
assert f.seek(100) == 100 and f.read() == ""
raises(ValueError, f.seek, -1)
e = raises(io.UnsupportedOperation, f.seek, 1, 1)
assert isinstance(e, OSError) and isinstance(e, ValueError)
raises(io.UnsupportedOperation, f.seek, 1, 2)
raises(io.UnsupportedOperation, f.write, "x")
f.close()
for name in ("tell", "readable", "seekable", "read"):
    raises(ValueError, getattr(f, name))
raises(ValueError, f.seek, 0)

# Binary read mode.
f = open(path, "rb")
assert (f.readable(), f.writable(), f.seekable()) == (True, False, True)
assert f.read(3) == b"h\xc3\xa9" and f.tell() == 3
assert f.seek(-1, 1) == 2 and f.read(1) == b"\xa9"
assert f.seek(-6, 2) == size - 6 and f.read() == b"world\n"
assert f.seek(4) == 4 and f.readline() == b"lo\n"
assert f.seek(50) == 50 and f.read() == b""
raises(OSError, f.seek, -1)
raises(OSError, f.seek, -100, 1)
f.close()
raises(ValueError, f.tell)

# Descriptor files (writing and read-write modes).
f = open(path, "a")
assert f.tell() == size, "append mode starts at the end"
assert (f.readable(), f.writable(), f.seekable()) == (False, True, True)
f.close()
other = os.path.join(d, "rw.bin")
with open(other, "w+b") as f:
    assert f.write(b"abcdef") == 6 and f.tell() == 6
    assert f.seek(2) == 2 and f.read(2) == b"cd" and f.tell() == 4
    assert (f.readable(), f.writable(), f.seekable()) == (True, True, True)

# In-memory streams.
for s in (io.StringIO("ab"), io.BytesIO(b"ab")):
    assert (s.readable(), s.writable(), s.seekable()) == (True, True, True)
    assert s.seek(1) == 1 and s.tell() == 1
    raises(io.UnsupportedOperation, s.fileno)

# The standard streams.
assert sys.stdout.writable() and not sys.stdout.readable()
assert sys.stderr.writable() and not sys.stderr.readable()
assert sys.stdin.readable() and not sys.stdin.writable()
assert (sys.stdin.fileno(), sys.stdout.fileno(), sys.stderr.fileno()) == (0, 1, 2)
for stream in (sys.stdin, sys.stdout, sys.stderr):
    assert stream.seekable() in (True, False)
    if not stream.seekable():
        raises(OSError, stream.tell)

# The motivating case: tracebacks show the source line again.
def fail():
    raise ValueError("seek-tell-marker")


try:
    fail()
except ValueError:
    text = traceback.format_exc()
assert 'raise ValueError("seek-tell-marker")' in text, text

os.remove(path)
os.remove(other)
os.rmdir(d)
print("file_seek_tell: ok")
