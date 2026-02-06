"""Print thread IDs from main and 4 workers; each worker does sum(range(1000))."""
import _thread
import threading

def f(i):
    _thread.log_thread_ident("w%d" % i)
    return sum(range(1000))

_thread.log_thread_ident("main")
threads = [threading.Thread(target=f, args=(i,)) for i in range(4)]
for t in threads:
    t.start()
for t in threads:
    t.join()
print("done")
