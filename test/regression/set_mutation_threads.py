# Regression test: set mutations from parallel threads must not lose or
# duplicate elements.
#
# Every set mutator is a read-modify-write of the set's `__data__` payload.
# Without a compare-and-swap, a write landing between another thread's read
# and write dropped that thread's update. CPython makes these operations
# atomic; a GIL-free runtime has to rebuild that guarantee.
import threading

N_THREADS = 4
N_ITEMS = 2000


def run_threads(target):
    threads = [threading.Thread(target=target, args=(k,)) for k in range(N_THREADS)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()


shared = set()


def adder(k):
    for i in range(N_ITEMS):
        shared.add((k, i))


run_threads(adder)
assert len(shared) == N_THREADS * N_ITEMS, \
    "set.add lost %d of %d elements" % (N_THREADS * N_ITEMS - len(shared), N_THREADS * N_ITEMS)


def discarder(k):
    for i in range(N_ITEMS):
        shared.discard((k, i))


run_threads(discarder)
assert len(shared) == 0, "set.discard left %d elements" % len(shared)

bulk = set()


def updater(k):
    for i in range(N_ITEMS // 4):
        bulk.update([(k, i, 0), (k, i, 1)])


run_threads(updater)
assert len(bulk) == N_THREADS * (N_ITEMS // 4) * 2, "set.update lost elements: %d" % len(bulk)

pool = set(range(N_THREADS * N_ITEMS))
popped = []


def popper(k):
    while True:
        try:
            item = pool.pop()
        except KeyError:
            return
        popped.append(item)


run_threads(popper)
assert len(pool) == 0, "set.pop left %d elements" % len(pool)
assert len(popped) == N_THREADS * N_ITEMS, \
    "set.pop returned %d elements for %d" % (len(popped), N_THREADS * N_ITEMS)
assert len(set(popped)) == len(popped), "set.pop returned the same element twice"

print("set mutation threads OK")
