"""
A small Richards-style benchmark — OO with method dispatch.

Creates a chain of "task" objects, each forwarding work to the next via
a virtual `run` method.  Stresses: class instantiation, attribute
access, method dispatch, instance state mutation.  Pure Python.
"""
import sys
import time


class Counter(object):
    def __init__(self):
        self.n = 0
        self.total = 0

    def step(self, value):
        self.n += 1
        self.total += value


class Worker(object):
    def __init__(self, ident, counter):
        self.ident = ident
        self.counter = counter
        self.next = None

    def run(self, value):
        # Forward to next worker if any, else terminate at counter.
        if self.next is None:
            self.counter.step(value + self.ident)
        else:
            self.next.run(value + self.ident)


def build_chain(length, counter):
    head = Worker(0, counter)
    cur = head
    i = 1
    while i < length:
        nxt = Worker(i, counter)
        cur.next = nxt
        cur = nxt
        i += 1
    return head


def workload(rounds, chain_len):
    counter = Counter()
    chain = build_chain(chain_len, counter)
    r = 0
    while r < rounds:
        chain.run(r)
        r += 1
    return counter.total


def main():
    rounds = 200
    chain_len = 20
    if len(sys.argv) > 1:
        rounds = int(sys.argv[1])
    if len(sys.argv) > 2:
        chain_len = int(sys.argv[2])

    workload(rounds, chain_len)  # warmup
    times = []
    for _ in range(5):
        t0 = time.perf_counter()
        result = workload(rounds, chain_len)
        times.append((time.perf_counter() - t0) * 1000)
    times.sort()
    print("richards_lite rounds=%d chain=%d  min=%.1fms  median=%.1fms  total=%d" %
          (rounds, chain_len, times[0], times[2], result))


if __name__ == "__main__":
    main()
