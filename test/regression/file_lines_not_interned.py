# Lines read from a file, and the strings str methods compute from them, are
# ordinary collectable objects, not interned symbols.
#
# A read-mode file returned every read(), readline(), readlines() and
# iteration result through PythonEnvironment::getInternedString, as did most
# str methods (split, strip, partition, replace, lower, ...). Each distinct
# line therefore became a permanent entry of protoCore's intern table, and as
# that table grew each lookup in it slowed down: iterating a file by lines was
# superlinear (50,000 lines took 3.0 s, 200,000 took 83.6 s) and its memory
# was never returned.
#
# Identity is the observable consequence: two equal results must be distinct
# objects. Strings of up to 6 UTF-8 bytes are stored inside the pointer by
# protoCore, so for those `is` is value equality and proves nothing; every
# string checked here is longer.

import os
import sys
import tempfile


LINE = "2026-10-01T12:00:00Z INFO GET /api/items/%d 200 latency=%dms\n"

fd, path = tempfile.mkstemp(suffix=".log")
os.close(fd)
try:
    with open(path, "w") as f:
        f.write("first_line_of_the_file\n")
        f.write("first_line_of_the_file\n")
        f.write("with\0a NUL character inside\n")
        for i in range(40000):
            f.write(LINE % (i, i % 900))

    # Equal lines are distinct objects, for every way of reading them.
    with open(path) as f:
        a = f.readline()
        b = f.readline()
        assert a == b == "first_line_of_the_file\n"
        assert a is not b, "equal readline() results are one interned object"
        # A NUL character is kept: the line used to be cut at it.
        nul = f.readline()
        assert nul == "with\0a NUL character inside\n", repr(nul)
    with open(path) as f:
        a = next(f)
        b = next(f)
        assert a is not b, "equal lines from iteration are one interned object"
    with open(path) as f:
        lines = f.readlines()
        assert lines[0] is not lines[1], "readlines() results are interned"
        assert len(lines) == 40003
    with open(path) as f:
        r1 = f.read(22)
    with open(path) as f:
        r2 = f.read(22)
    assert r1 == r2 == "first_line_of_the_file" and r1 is not r2, "read() results are interned"

    # str methods over those lines: split, strip, partition, replace, lower,
    # upper, splitlines and slicing-like helpers.
    s = "alpha_beta_gamma delta_epsilon_zeta"
    for name, call in [
        ("split", lambda: s.split()[0]),
        ("split(sep)", lambda: s.split(" ")[1]),
        ("rsplit", lambda: s.rsplit(" ", 1)[0]),
        ("strip", lambda: ("  " + s + "  ").strip()),
        ("partition", lambda: s.partition(" ")[2]),
        ("replace", lambda: s.replace("alpha", "omega")),
        ("lower", lambda: s.upper().lower()),
        ("upper", lambda: s.upper()),
        ("splitlines", lambda: (s + "\n" + s).splitlines()[1]),
        ("removeprefix", lambda: s.removeprefix("alpha_")),
    ]:
        x, y = call(), call()
        assert x == y, name
        assert x is not y, "equal str.%s results are one interned object" % name

finally:
    os.unlink(path)

print("file_lines_not_interned: ok")
