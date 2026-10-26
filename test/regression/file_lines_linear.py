# Reading a file by lines is linear in the number of lines.
#
# Clock-dependent: its verdict is a time ratio, so CI runs it in the timing job
# (CLOCK_DEPENDENT_TESTS), never as a gate. The identity checks that pin the
# cause (interned lines) are in file_lines_not_interned.py, which gates.
#
# With every line interned, iterating a file was superlinear: 10,000 lines took
# 152 ms and 50,000 took 3,556 ms; four times the lines took about sixteen
# times as long. The bound below leaves room for a loaded machine.

import os
import tempfile
import time

LINE = "2026-10-01T12:00:00Z INFO GET /api/items/%d 200 latency=%dms\n"

fd, path = tempfile.mkstemp(suffix=".log")
os.close(fd)
try:
    with open(path, "w") as f:
        for i in range(40000):
            f.write(LINE % (i, i % 900))

    def read_lines(limit):
        t0 = time.perf_counter()
        n = 0
        with open(path) as f:
            for line in f:
                n += 1
                if n == limit:
                    break
        assert n == limit
        return time.perf_counter() - t0

    read_lines(10000)  # warm-up
    small = min(read_lines(10000) for _ in range(3))
    large = min(read_lines(40000) for _ in range(3))
    ratio = large / small
    print("file_lines_linear: 10,000 lines %.1f ms, 40,000 lines %.1f ms, ratio %.2f"
          % (small * 1000, large * 1000, ratio))
    assert ratio < 9.0, "reading 4x the lines took %.1fx as long" % ratio
finally:
    os.unlink(path)

print("file_lines_linear: ok")
