# Text-mode writes translate "\n" to os.linesep, as CPython: "\r\n" on
# Windows, unchanged elsewhere. newline="" and newline="\n" write "\n" as is;
# any other newline value replaces every "\n". Binary mode never translates.
# Reading back in text mode gives "\n" in every case (universal newlines).
import os
import tempfile

d = tempfile.mkdtemp()
p = os.path.join(d, "t.txt")


def raw():
    with open(p, "rb") as r:
        return r.read()


def check(expected, **kw):
    for mode in ("w", "w+", "x", "a"):
        if os.path.exists(p):
            os.remove(p)
        with open(p, mode, **kw) as f:
            f.write("a\nb\n")
            f.writelines(["c\n"])
        got = raw()
        assert got == expected, (mode, kw, got, expected)
        with open(p) as r:
            assert r.read() == "a\nb\nc\n", (mode, kw)


native = os.linesep.encode()
check(b"a" + native + b"b" + native + b"c" + native)
check(b"a\nb\nc\n", newline="")
check(b"a\nb\nc\n", newline="\n")
check(b"a\r\nb\r\nc\r\n", newline="\r\n")
check(b"a\rb\rc\r", newline="\r")

with open(p, "wb") as f:
    f.write(b"x\ny\n")
assert raw() == b"x\ny\n"

# print() to a text file goes through the same translation.
with open(p, "w") as f:
    print("p", file=f)
assert raw() == b"p" + native, raw()

os.remove(p)
os.rmdir(d)
print("text_newline_write: ok")
