import sys
import threading


def probe(v):
    return v > 100


def loop():
    total = 0
    for i in range(1, 7):
        total += i
    return total


def raising():
    for kind in (ValueError, KeyError):
        try:
            raise kind("raised %s" % kind.__name__)
        except (ValueError, KeyError):
            pass


def worker(k):
    mine = k * 10
    print("worker", k, mine, flush=True)


if "threads" in sys.argv:
    ts = [threading.Thread(target=worker, args=(k,)) for k in (1, 2)]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
else:
    raising()
    print("total", loop())
