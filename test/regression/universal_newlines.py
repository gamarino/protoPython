# Text mode reads with universal newlines, as CPython does on every platform:
# with newline=None (the default) "\r\n" and a lone "\r" both read as "\n", so a
# file written on Windows (or a CRLF checkout) reads the same everywhere.
# newline="" keeps the line endings untouched.

import os
import tempfile

path = os.path.join(tempfile.gettempdir(), "protopy_universal_newlines.txt")
with open(path, "wb") as f:
    f.write(b"one\r\ntwo\rthree\nfour")

try:
    with open(path) as f:
        assert f.read() == "one\ntwo\nthree\nfour", "read() must translate CRLF and CR"
    with open(path) as f:
        assert f.readlines() == ["one\n", "two\n", "three\n", "four"], "readlines() must translate"
    with open(path, newline="") as f:
        assert f.read() == "one\r\ntwo\rthree\nfour", "newline='' must not translate"
    with open(path, "rb") as f:
        assert f.read() == b"one\r\ntwo\rthree\nfour", "binary mode must not translate"
finally:
    os.remove(path)

print("ok")
