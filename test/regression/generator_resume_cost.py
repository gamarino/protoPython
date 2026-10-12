# Resuming a generator costs a small multiple of a function call, and a value
# passing through a chain of `yield from` costs that much per level.
#
# A resume used to copy every slot of the generator's context, the operand
# stack area included (128 + max stack), into a new list, and rebuild a list
# of local names of that size: ~70 us per resume against ~2 us for a call, so
# a recursive `yield from` of depth 200 took ~3 s.  The test compares costs
# measured in the same process, so it holds on slow and instrumented builds:
# resume / call was ~330 and a yield-from hop / call ~500; they are ~11 and
# ~19 now (limits 60 and 100).
import time

N = 3000


def best_of(fn, runs=5):
    best = None
    for _ in range(runs):
        t = time.perf_counter()
        fn()
        dt = time.perf_counter() - t
        best = dt if best is None else min(best, dt)
    return best


def f(x):
    return x


def calls():
    s = 0
    for i in range(N):
        s += f(i)
    assert s == N * (N - 1) // 2


def gen(n):
    for i in range(n):
        yield i


def resumes():
    s = 0
    for x in gen(N):
        s += x
    assert s == N * (N - 1) // 2


def walk(n):
    if n > 0:
        yield n
        yield from walk(n - 1)


DEPTH = 120
HOPS = DEPTH * (DEPTH + 1) // 2   # a value at depth d passes d levels


def chain():
    assert sum(walk(DEPTH)) == DEPTH * (DEPTH + 1) // 2


def hop_calls():
    s = 0
    for i in range(HOPS):
        s += f(i)
    assert s == HOPS * (HOPS - 1) // 2


call_cost = best_of(calls) / N
resume_cost = best_of(resumes) / N
hop_call_cost = best_of(hop_calls) / HOPS
hop_cost = best_of(chain, 3) / HOPS
print("resume / call = %.1f" % (resume_cost / call_cost))
print("yield-from hop / call = %.1f" % (hop_cost / hop_call_cost))
assert resume_cost < 60 * call_cost, (resume_cost, call_cost)
assert hop_cost < 100 * hop_call_cost, (hop_cost, hop_call_cost)
print("ok")
