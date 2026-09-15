# open() with a path in the "w", "a" and "x" modes writes to the file.
import os
import tempfile

base = tempfile.mkdtemp()
try:
    path = os.path.join(base, "out.txt")

    with open(path, "w") as f:
        assert f.write("first\n") == 6
        assert f.write("é\n") == 2          # characters, not bytes
        assert f.writable()
        assert not f.closed
    assert f.closed
    assert os.path.getsize(path) == 9
    with open(path) as f:
        assert f.read() == "first\né\n"

    # "w" truncates.
    f = open(path, "w")
    f.write("new")
    f.close()
    with open(path) as f:
        assert f.read() == "new"

    # "a" appends.
    with open(path, "a") as f:
        f.write(" tail")
    with open(path) as f:
        assert f.read() == "new tail"

    # "x" creates, and fails when the file exists.
    fresh = os.path.join(base, "fresh.txt")
    with open(fresh, "x") as f:
        f.write("x")
    assert os.path.exists(fresh)
    try:
        open(fresh, "x")
    except FileExistsError:
        pass
    else:
        raise AssertionError("mode 'x' on an existing file must raise FileExistsError")

    # Binary write.
    binpath = os.path.join(base, "data.bin")
    with open(binpath, "wb") as f:
        f.write(b"\x00\x01\xff")
    assert os.path.getsize(binpath) == 3

    # Errors are reported.
    try:
        open(os.path.join(base, "missing", "f.txt"), "w")
    except FileNotFoundError:
        pass
    else:
        raise AssertionError("expected FileNotFoundError")
    try:
        open(base, "w")
    except IsADirectoryError:
        pass
    else:
        raise AssertionError("expected IsADirectoryError")
finally:
    for name in os.listdir(base):
        os.remove(os.path.join(base, name))
    os.rmdir(base)

print("io_open_write_modes: ok")
