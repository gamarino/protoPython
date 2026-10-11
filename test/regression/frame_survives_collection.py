# Regression test: a running function's frame must survive a collection.
#
# The frame of a Python call is a cell of the call's protoCore context and
# was held only in C++ locals (runUserFunctionCall's `frame`, the
# interpreter's `frame` parameter, the thread-local current frame).  The
# interpreter's periodic ctx->safepoint() hands the context's young cells to
# the collector once the context has allocated more than
# PROTOCORE_GC_CONTEXT_THRESHOLD cells, so a long-running function's frame
# became a collection candidate with nothing tracing it; the next cycle freed
# it, the cell was reused, and LOAD_DEREF / STORE_DEREF walked a stranger:
# "NameError: name 'target' is not defined" in a nested function, at random.
#
# CTest runs this file with a tiny context threshold and a small heap
# limit (see CMakeLists.txt), which makes every call below cross the
# threshold many times and run several cycles: before the fix it failed on
# every run.
import sys

ROUNDS = 5
N = 20000


def read_free_variable(n):
    # `target` is a free variable of worker(): LOAD_DEREF walks worker's
    # frame to the closure frame that holds it.
    target = [0]

    def worker():
        for i in range(n):
            target.append(i)
        return len(target)

    return worker()


def write_free_variable(n):
    # `nonlocal` writes go through STORE_DEREF on the same frame chain.
    count = 0

    def bump():
        nonlocal count
        for i in range(n):
            count += 1
            [i, i]          # allocate, so the threshold is crossed
        return count

    return bump()


def nested_definition_after_allocation(n):
    # The outer frame is read by BUILD_FUNCTION after the allocations.
    junk = []
    for i in range(n):
        junk.append([i])
    base = len(junk)

    def inner():
        return base + 1

    return inner()


for _ in range(ROUNDS):
    assert read_free_variable(N) == N + 1
    assert write_free_variable(N) == N
    assert nested_definition_after_allocation(N) == N + 1

print("frame survives collection OK")
sys.exit(0)
