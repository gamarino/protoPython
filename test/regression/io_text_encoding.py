# io.text_encoding follows CPython, so pathlib.Path.read_text and write_text
# (which call it) work.
import io
import os
import pathlib
import sys
import tempfile

# None selects the default text encoding: "locale", or "utf-8" in UTF-8 mode.
expected_default = "utf-8" if sys.flags.utf8_mode else "locale"
assert io.text_encoding(None) == expected_default, io.text_encoding(None)
assert io.text_encoding(None, 3) == expected_default
# An explicit encoding is returned unchanged.
assert io.text_encoding("utf-8") == "utf-8"
assert io.text_encoding("latin-1", 2) == "latin-1"
try:
    io.text_encoding()
except TypeError:
    pass
else:
    raise AssertionError("text_encoding() without arguments must raise TypeError")

base = pathlib.Path(tempfile.mkdtemp())
try:
    path = base / "note.txt"
    assert path.write_text("first line\nsecond line\n") == 23
    assert path.exists()
    assert os.path.getsize(path) == 23
    assert path.read_text() == "first line\nsecond line\n"
    assert path.read_text(encoding="utf-8").splitlines() == ["first line", "second line"]
    path.write_text("replaced", encoding="utf-8")
    assert path.read_text() == "replaced"
    path.unlink()
    assert not path.exists()
finally:
    for name in os.listdir(base):
        os.remove(os.path.join(base, name))
    os.rmdir(base)

print("io_text_encoding: ok")
