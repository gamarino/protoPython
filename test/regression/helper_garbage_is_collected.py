# Regression test: what native helpers allocate during a call is collectable
# while the caller keeps running.
#
# The helpers that iterate, raise or call (env->iter / next, exception
# construction, the pending-exception slot, ...) allocated in the thread's
# Python context, which is set where code starts running (module execution)
# and which function calls did not update.  So during main() below every
# iterator and exception joined the MODULE context's young generation, a GC
# root submitted only at a module-level safepoint, i.e. after main() returns.
# Nothing a long-running function did through those helpers could be
# collected: each loop below exhausted a 2.5 M-cell heap (CTest sets
# PROTOCORE_HEAP_LIMIT_CELLS) before the fix.
XS = [1, 2, 3]


def loop_over_list():
    t = 0
    for x in XS:
        t += x
    return t


def iter_and_next():
    it = iter(XS)
    return next(it) + next(it) + next(it)


def raise_and_catch():
    try:
        raise ValueError("x")
    except ValueError:
        return 6


def comprehension(k):
    return sum([x * k for x in XS])


def main(n):
    total = 0
    for i in range(n):
        total += loop_over_list() + iter_and_next() + raise_and_catch()
        total += comprehension(1)
    return total


N = 30000
assert main(N) == 24 * N
print("helper garbage is collected OK")
