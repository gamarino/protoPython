# Regression test: list mutations from parallel threads must not lose or
# duplicate elements.
#
# Every list mutator is a read-modify-write of the list's `__data__`
# payload. Without a compare-and-swap, a write landing between another
# thread's read and write dropped that thread's update: `results.append(i)`
# from four threads lost elements, and two threads could pop the same item.
# CPython makes these operations atomic (with the GIL, and with per-object
# locking on 3.14t); a GIL-free runtime has to rebuild that guarantee.
import threading

N_THREADS = 4
N_ITEMS = 2000


def run_threads(target, args_for):
    threads = [threading.Thread(target=target, args=args_for(k)) for k in range(N_THREADS)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()


shared = []


def appender(tag):
    for i in range(N_ITEMS):
        shared.append((tag, i))


run_threads(appender, lambda k: (k,))
assert len(shared) == N_THREADS * N_ITEMS, \
    "list.append lost %d of %d items" % (N_THREADS * N_ITEMS - len(shared), N_THREADS * N_ITEMS)

work = list(range(N_THREADS * N_ITEMS))
popped = []


def popper():
    while True:
        try:
            item = work.pop()
        except IndexError:
            return
        popped.append(item)


run_threads(popper, lambda k: ())
assert work == [], "items left after concurrent pop: %d" % len(work)
assert len(popped) == N_THREADS * N_ITEMS, \
    "list.pop returned %d items for %d elements" % (len(popped), N_THREADS * N_ITEMS)
assert sorted(popped) == list(range(N_THREADS * N_ITEMS)), "list.pop duplicated or lost items"

extended = []


def extender(tag):
    for i in range(N_ITEMS // 4):
        extended.extend([tag, i])


run_threads(extender, lambda k: (k,))
assert len(extended) == N_THREADS * (N_ITEMS // 4) * 2, \
    "list.extend lost items: %d" % len(extended)

print("list mutation threads OK")
