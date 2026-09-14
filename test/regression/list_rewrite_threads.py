# Regression test: operations that rewrite a list's payload must not undo
# appends made by another thread.
#
# `lst[i] = x` (the STORE_SUBSCR fast path for exact lists) and
# list.reverse / sort / `*=` derived the new payload from the `__data__` they
# had read and stored it back with a plain setAttribute, so an append
# published by another thread in between was silently dropped. They now
# publish with compare-and-swap: the fast path falls back to
# list.__setitem__ on a lost race, reverse and `*=` retry, and sort reports
# the concurrent change with ValueError like CPython.
import threading

N_APPENDS = 20000


def race(mutator, n_appends):
    target = [0]
    done = [False]

    def rewriter():
        while not done[0]:
            mutator(target)

    def appender():
        for i in range(n_appends):
            target.append(i + 1)
        done[0] = True

    w = threading.Thread(target=rewriter)
    a = threading.Thread(target=appender)
    w.start()
    a.start()
    a.join()
    w.join()
    return target


def store_first(lst):
    lst[0] = 0


stored = race(store_first, N_APPENDS)
assert len(stored) == N_APPENDS + 1, \
    "lst[0] = x dropped %d of %d appends" % (N_APPENDS + 1 - len(stored), N_APPENDS)
assert stored[1:] == list(range(1, N_APPENDS + 1)), "appended items lost or reordered"


def reverse(lst):
    lst.reverse()


reversed_list = race(reverse, 3000)
assert len(reversed_list) == 3001, \
    "list.reverse dropped %d of %d appends" % (3001 - len(reversed_list), 3000)
assert sorted(reversed_list) == list(range(3001)), "list.reverse lost or duplicated items"

print("list rewrite threads OK")
