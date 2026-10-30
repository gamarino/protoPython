# Recursion past a thread's own stack raises RecursionError.  The native
# stack guard assumed every thread had the main thread's stack (8 MiB);
# a Python thread on macOS has 512 KiB, and one started after
# threading.stack_size(n) has n, so deep recursion there crashed.
import sys
import threading

sys.setrecursionlimit(100_000)
assert threading.stack_size() == 0
old = threading.stack_size(512 * 1024)
assert old == 0
try:
    threading.stack_size(1000)
except ValueError:
    pass
else:
    raise AssertionError("stack_size accepted 1000 bytes")

result = []


deepest = [0]


def down(n, depth=0):
    if depth > deepest[0]:
        deepest[0] = depth
    return 0 if n == 0 else 1 + down(n - 1, depth + 1)


def worker():
    try:
        down(90_000)
        result.append("no error")
    except RecursionError:
        result.append("RecursionError")
    # The thread recovers: half the depth the guard allowed works again.
    # The depth differs by platform (about 76 calls with GCC on Linux; the
    # guard keeps half the stack free on Windows instead of a quarter).
    # Recorded rather than raised, so a failure here shows in the assertion.
    again = max(1, deepest[0] // 2)
    try:
        result.append(down(again) == again)
    except BaseException as e:
        result.append(repr(e))


t = threading.Thread(target=worker)
t.start()
t.join()
threading.stack_size(0)
# CPython does not use the C stack for Python-to-Python calls and finishes;
# protoPython does and must raise RecursionError instead of crashing.
assert result[0] in ("RecursionError", "no error") and result[1] is True, (result, deepest)
sys.setrecursionlimit(1000)
print("thread_deep_recursion: depth reached in a 512 KiB thread:", deepest[0])
print("thread_deep_recursion: ok")
