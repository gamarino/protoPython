# Regression test: dict mutations from parallel threads must not lose
# updates or leave keys and values out of sync.
#
# A dict keeps its values in `__data__` and its insertion-ordered keys in
# `__keys__`; every mutator is a read-modify-write of both. Without an
# atomic publish of the pair, a write landing between another thread's read
# and write dropped that thread's update or desynchronised the two
# attributes. CPython makes these operations atomic; a GIL-free runtime has
# to rebuild that guarantee.
import threading

N_THREADS = 4
# Small on purpose: inserting a new key currently scans every stored key, so
# building a dict is quadratic. A few hundred keys per thread already expose
# the lost updates.
N_ITEMS = 300
TOTAL = N_THREADS * N_ITEMS


def run_threads(target):
    threads = [threading.Thread(target=target, args=(k,)) for k in range(N_THREADS)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()


def check_in_sync(d, label):
    keys = list(d.keys())
    assert len(keys) == len(d), "%s: %d keys but len() is %d" % (label, len(keys), len(d))
    assert len(set(keys)) == len(keys), "%s: duplicated keys" % label


shared = {}


def setter(k):
    for i in range(N_ITEMS):
        shared[(k, i)] = i


run_threads(setter)
assert len(shared) == TOTAL, "dict.__setitem__ lost %d of %d entries" % (TOTAL - len(shared), TOTAL)
check_in_sync(shared, "after __setitem__")
for k in range(N_THREADS):
    for i in range(N_ITEMS):
        assert shared[(k, i)] == i, "wrong value for %r" % ((k, i),)


def deleter(k):
    for i in range(N_ITEMS):
        del shared[(k, i)]


run_threads(deleter)
assert len(shared) == 0, "del left %d entries" % len(shared)
check_in_sync(shared, "after del")

merged = {}


def updater(k):
    for i in range(N_ITEMS // 4):
        merged.update({(k, i): 1, (k, i, "x"): 2})


run_threads(updater)
assert len(merged) == N_THREADS * (N_ITEMS // 4) * 2, "dict.update lost entries: %d" % len(merged)
check_in_sync(merged, "after update")

defaults = {}


def setdefaulter(k):
    for i in range(N_ITEMS):
        defaults.setdefault(i, k)


run_threads(setdefaulter)
assert len(defaults) == N_ITEMS, "dict.setdefault produced %d entries for %d keys" % (len(defaults), N_ITEMS)
check_in_sync(defaults, "after setdefault")

pool = {i: i for i in range(TOTAL)}
popped = []


def popper(k):
    while True:
        try:
            key, value = pool.popitem()
        except KeyError:
            return
        popped.append(key)


run_threads(popper)
assert len(pool) == 0, "dict.popitem left %d entries" % len(pool)
assert sorted(popped) == list(range(TOTAL)), "dict.popitem lost or duplicated entries"

print("dict mutation threads OK")
