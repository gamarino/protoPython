"""deque mutators called from parallel threads keep every item exactly once."""
import threading
from collections import deque

N_THREADS = 4
N_ITEMS = 3000

d = deque()


def producer(base):
    for i in range(N_ITEMS):
        if i % 2:
            d.append(base + i)
        else:
            d.appendleft(base + i)


threads = [threading.Thread(target=producer, args=(t * N_ITEMS,)) for t in range(N_THREADS)]
for t in threads:
    t.start()
for t in threads:
    t.join()
assert len(d) == N_THREADS * N_ITEMS, len(d)
assert sorted(d) == list(range(N_THREADS * N_ITEMS))

taken = []
lock = threading.Lock()


def consumer():
    local = []
    while True:
        try:
            local.append(d.popleft() if len(local) % 2 else d.pop())
        except IndexError:
            break
    with lock:
        taken.extend(local)


threads = [threading.Thread(target=consumer) for _ in range(N_THREADS)]
for t in threads:
    t.start()
for t in threads:
    t.join()
assert len(d) == 0 and sorted(taken) == list(range(N_THREADS * N_ITEMS)), (len(d), len(taken))

bounded = deque(maxlen=100)
threads = [threading.Thread(target=lambda: [bounded.append(i) for i in range(2000)]) for _ in range(N_THREADS)]
for t in threads:
    t.start()
for t in threads:
    t.join()
assert len(bounded) == 100

it = iter(deque([1, 2, 3]))
dd = deque([1, 2, 3])
it = iter(dd)
next(it)
dd.append(4)
try:
    next(it)
except RuntimeError:
    pass
else:
    raise AssertionError("mutating a deque during iteration must raise RuntimeError")

print("deque threads OK")
