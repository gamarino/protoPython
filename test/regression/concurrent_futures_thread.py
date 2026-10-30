# concurrent.futures.thread imports and its executor runs work.  The module
# registers lock._at_fork_reinit with os.register_at_fork at import time;
# the lock types had no such method, so the import failed (and with it
# ThreadPoolExecutor and unittest.mock's ThreadingMock tests).
import concurrent.futures
import concurrent.futures.thread as cft
import threading

for lock in (threading.Lock(), threading.RLock()):
    lock.acquire()
    lock._at_fork_reinit()
    assert lock.acquire(False), "reinitialised lock is not free"
    lock.release()

# A bound method stored on a class keeps its receiver (method.__get__
# returns the method), as ThreadPoolExecutor's _counter does.
import itertools


class Holder:
    _counter = itertools.count().__next__

    def take(self):
        return self._counter()


assert (Holder().take(), Holder().take()) == (0, 1)
bound = Holder().take
assert bound.__get__(object()) is bound

with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
    results = list(ex.map(lambda x: x * x, range(20)))
assert results == [x * x for x in range(20)], results
assert cft.ThreadPoolExecutor is concurrent.futures.ThreadPoolExecutor
print("concurrent_futures_thread: ok")
