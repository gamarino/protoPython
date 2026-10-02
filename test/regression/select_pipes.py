# select.select over pipes: a timeout is honoured, readiness is reported, and
# descriptor sets that select() cannot represent raise ValueError, as CPython.
import os
import select
import sys
import time

r, w = os.pipe()
try:
    # Nothing to read: select waits for the timeout and reports nothing.
    start = time.monotonic()
    ready = select.select([r], [], [], 0.3)
    elapsed = time.monotonic() - start
    assert ready == ([], [], []), ready
    assert 0.25 <= elapsed < 5.0, elapsed

    # A zero timeout polls.
    assert select.select([r], [], [], 0) == ([], [], [])

    # The write end of a pipe is writable.
    assert select.select([], [w], [], 0)[1] == [w]

    # Data makes the read end readable at once, also with no timeout.
    os.write(w, b"x")
    start = time.monotonic()
    ready = select.select([r], [], [], 10)
    assert ready[0] == [r], ready
    assert time.monotonic() - start < 5.0
    assert select.select([r], [], [])[0] == [r]
    assert os.read(r, 1) == b"x"

    # A closed writer makes the reader readable (end of file).
    os.close(w)
    w = -1
    assert select.select([r], [], [], 5)[0] == [r]
    assert os.read(r, 1) == b""
finally:
    os.close(r)
    if w >= 0:
        os.close(w)

# Sets select() cannot hold raise ValueError instead of being truncated.
if sys.platform == "win32":
    # At most FD_SETSIZE (512) entries per set, as CPython on Windows.
    r2, w2 = os.pipe()
    try:
        try:
            select.select([r2] * 513, [], [], 0)
        except ValueError:
            pass
        else:
            raise AssertionError("513 descriptors did not raise ValueError")
        assert select.select([r2] * 512, [], [], 0) == ([], [], [])
    finally:
        os.close(r2)
        os.close(w2)
else:
    # A descriptor at or above FD_SETSIZE does not fit an fd_set.
    try:
        select.select([1 << 20], [], [], 0)
    except ValueError:
        pass
    else:
        raise AssertionError("an out-of-range descriptor did not raise ValueError")

# A negative descriptor is a ValueError too.
try:
    select.select([-1], [], [], 0)
except ValueError:
    pass
else:
    raise AssertionError("a negative descriptor did not raise ValueError")

print("select_pipes: ok")
