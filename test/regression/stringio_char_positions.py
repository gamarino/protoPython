# io.StringIO counts characters, not UTF-8 bytes: read(n) returns n
# characters, and tell() / seek() positions are character indices, as in
# CPython's StringIO. Every case uses non-ASCII text, where the two differ.

import io

s = io.StringIO("héllo wörld €uro 😀!")
assert s.read(2) == "hé", "read(2) must return two characters"
assert s.tell() == 2, s.tell()
assert s.read(3) == "llo"
assert s.tell() == 5
s.seek(6)
assert s.read(5) == "wörld"
assert s.tell() == 11
s.seek(12)
assert s.read(1) == "€"
s.seek(17)
assert s.read(1) == "😀"
assert s.tell() == 18
assert s.read() == "!"
assert s.tell() == 19 == len("héllo wörld €uro 😀!")
assert s.read(5) == ""

# seek(0, 2) goes to the end, measured in characters.
assert s.seek(0, 2) == 19
assert s.tell() == 19
assert s.seek(0) == 0
assert s.seek(0, 1) == 0

# write() returns the number of characters written and moves the position by
# that many characters; overwriting replaces characters, not bytes.
w = io.StringIO()
assert w.write("ñandú") == 5
assert w.tell() == 5
w.seek(1)
assert w.write("A") == 1
assert w.getvalue() == "ñAndú", w.getvalue()
w.seek(4)
assert w.write("€€") == 2
assert w.getvalue() == "ñAnd€€", w.getvalue()
assert w.tell() == 6

# Writing past the end pads with NUL characters, as CPython does.
p = io.StringIO("é")
p.seek(3)
p.write("ü")
assert p.getvalue() == "é\0\0ü", repr(p.getvalue())

# readline() and readline(size) count characters and advance tell() by them.
r = io.StringIO("ä€x\nzweite Zeile\n")
assert r.readline(2) == "ä€"
assert r.tell() == 2
assert r.readline() == "x\n"
assert r.tell() == 4
assert r.readline() == "zweite Zeile\n"
assert r.tell() == 17

# truncate(size) cuts at a character index and keeps the position.
t = io.StringIO("añobé")
t.seek(1)
assert t.truncate(3) == 3
assert t.getvalue() == "año"
assert t.tell() == 1
t.seek(0, 2)
assert t.tell() == 3

# CPython's seek rules: a negative absolute position is a ValueError, and
# relative seeks other than zero are not supported (OSError).
try:
    s.seek(-1)
except ValueError:
    pass
else:
    raise AssertionError("seek(-1) must raise ValueError")
for whence in (1, 2):
    try:
        s.seek(1, whence)
    except OSError:
        pass
    else:
        raise AssertionError("seek(1, %d) must raise OSError" % whence)

print("stringio_char_positions: ok")
