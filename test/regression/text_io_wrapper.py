# io.TextIOWrapper over a binary buffer, and subprocess's text mode, which
# wraps the pipes in it. Both need the codec registry (_codecs).
import codecs
import io
import os
import sys

info = codecs.lookup("UTF-8")
assert info.name == "utf-8", info.name
assert codecs.lookup("latin-1").name == "iso8859-1"
assert codecs.getincrementaldecoder("utf-8")().decode(b"\xc3") == ""
dec = codecs.getincrementaldecoder("utf-8")()
assert dec.decode(b"h\xc3") == "h" and dec.decode(b"\xa9", final=True) == "é"
assert codecs.encode("é", "utf-8") == b"\xc3\xa9"
assert codecs.decode(b"\xe9", "latin-1") == "é"
try:
    codecs.lookup("no-such-codec")
except LookupError:
    pass
else:
    raise AssertionError("LookupError expected")
assert codecs.lookup_error("strict") is codecs.strict_errors

r, w = os.pipe()
os.write(w, "héllo\nworld\n".encode("utf-8"))
os.close(w)
t = io.TextIOWrapper(io.open(r, "rb", -1), encoding="utf-8")
assert t.readline() == "héllo\n"
assert t.read() == "world\n"
t.close()

if sys.platform != "win32":
    import subprocess
    res = subprocess.run(["echo", "hi"], capture_output=True, text=True)
    assert res.stdout == "hi\n", repr(res.stdout)
    p = subprocess.Popen(["cat"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    out, _ = p.communicate("abc\n")
    assert out == "abc\n", repr(out)

print("text_io_wrapper: ok")
