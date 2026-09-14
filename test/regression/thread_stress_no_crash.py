# Regression stress test: short-lived threads with allocation churn must
# not crash the interpreter.
#
# The per-thread getType cache and the LOAD_METHOD inline cache were keyed
# by object / type address and survived garbage collection. Once the GC
# freed an object its address came back as an unrelated object, which
# inherited the dead object's type (a Thread reported "'dict' object has
# no attribute '_initialized'") or a freed type cell that ensureClassFlags
# wrote into (SEGV in ProtoSparseList::rightRotate). Multi-threaded
# allocation made the reuse frequent enough to crash most runs.
#
# The counter update is deliberately unsynchronised: lost updates are
# expected on a GIL-free runtime and only the bounds are checked.
import threading

ROUNDS = 6
N_THREADS = 4
N_ITERS = 5000
counter = 0


def worker(seed):
    global counter
    scratch = []
    for i in range(N_ITERS):
        tmp = counter
        scratch.append({"i": i, "seed": seed})
        if len(scratch) > 64:
            scratch = []
        counter = tmp + 1


for r in range(ROUNDS):
    threads = [threading.Thread(target=worker, args=(r * 10 + k,)) for k in range(N_THREADS)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    for t in threads:
        assert t.is_alive() is False, "round %d left a live thread after join()" % r

total = ROUNDS * N_THREADS * N_ITERS
assert 0 < counter <= total, "counter %d outside (0, %d]" % (counter, total)
print("thread stress OK", counter, "of", total)
