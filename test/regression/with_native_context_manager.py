# Regression test: `with` on native context managers must run
# __enter__ and __exit__.
#
# OP_SETUP_WITH looked special methods up only in the Python MRO of the
# manager's type. `_thread` locks carry them on a protoCore prototype
# outside that MRO, so `with lock:` ran its body without acquiring or
# releasing the lock.
import _thread
import threading

lock = threading.Lock()
with lock:
    assert lock.locked() is True, "lock not held inside `with lock:`"
    assert lock.acquire(False) is False, "lock re-acquirable inside `with lock:`"
assert lock.locked() is False, "lock still held after `with lock:`"

outer = threading.Lock()
inner = threading.Lock()
with outer:
    with inner:
        assert outer.locked() is True, "nested `with`: outer lock not held"
        assert inner.locked() is True, "nested `with`: inner lock not held"
    assert inner.locked() is False, "inner lock not released by its `with`"
    assert outer.locked() is True, "outer lock released by the inner `with`"
assert outer.locked() is False, "outer lock not released"

raw = _thread.allocate_lock()
try:
    with raw:
        assert raw.locked() is True, "raw lock not held inside `with`"
        raise ValueError("boom")
except ValueError:
    pass
assert raw.locked() is False, "__exit__ did not release the lock on exception"

rl = _thread.RLock()
with rl:
    with rl:
        assert rl.locked() is True, "RLock not held inside nested `with`"
assert rl.locked() is False, "RLock still held after nested `with`"

N_THREADS = 4
N_ITERS = 2000
counter = 0
counter_lock = threading.Lock()


def worker():
    global counter
    for _ in range(N_ITERS):
        with counter_lock:
            counter += 1


threads = [threading.Thread(target=worker) for _ in range(N_THREADS)]
for t in threads:
    t.start()
for t in threads:
    t.join()
assert counter == N_THREADS * N_ITERS, "`with lock` lost updates: %d of %d" % (counter, N_THREADS * N_ITERS)

print("with native context manager OK")
