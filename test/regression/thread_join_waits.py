# Regression test: threading.Thread.join() must wait for the thread.
#
# `_thread.start_joinable_thread` looked its `handle=` keyword up by the
# symbol pointer, while keyword arguments are keyed by string hash, so
# threading.Thread's own handle never learned about the OS thread:
# join() returned immediately, is_alive() reported False while the
# thread was running, and the interpreter could exit under live workers.
import threading
import time

side_effect = []


def slow_worker():
    time.sleep(0.3)
    side_effect.append(1)


t = threading.Thread(target=slow_worker)
t.start()
assert t.is_alive() is True, "is_alive() while the worker sleeps"
t.join()
assert side_effect == [1], "Thread.join() returned before the thread finished"
assert t.is_alive() is False, "is_alive() after join()"

results = []


def staggered_worker(i):
    time.sleep(0.05 * (i + 1))
    results.append(i)


threads = [threading.Thread(target=staggered_worker, args=(i,)) for i in range(4)]
for th in threads:
    th.start()
for th in threads:
    th.join()
assert sorted(results) == [0, 1, 2, 3], "joined workers left results %r" % (results,)

event = threading.Event()
threading.Thread(target=event.set).start()
assert event.wait(5) is True, "Event set by another thread not observed"

print("thread join waits OK")
