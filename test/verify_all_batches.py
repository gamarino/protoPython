print("IMPORT START", flush=True)
import os
import _thread
import threading
import signal
import time
print("IMPORT DONE", flush=True)

def test_batch1():
    print("--- Batch 1: Builtins & Reflection ---", flush=True)
    g = globals()
    print("globals() type:", type(g), flush=True)
    print("globals() len:", len(g), flush=True)
    l = locals()
    print("locals() is globals():", l is g, flush=True)
    print("os.environ['TEST_VAR'] = 'Batch1'", flush=True)
    os.environ['TEST_VAR'] = 'Batch1'
    print("os.getenv('TEST_VAR'):", os.getenv('TEST_VAR'), flush=True)

def test_batch2_3():
    print("\n--- Batch 2 & 3: Concurrency ---", flush=True)
    lock = threading.Lock()
    evt = threading.Event()
    
    def worker():
        print("Worker starting...", flush=True)
        lock.acquire()
        print("Worker acquired lock", flush=True)
        lock.release()
        evt.set()
        print("Worker finished", flush=True)

    t = threading.Thread(target=worker)
    print("Starting thread...", flush=True)
    t.start()
    print("Thread started, is_alive:", t.is_alive(), flush=True)
    print("Waiting for event...", flush=True)
    evt.wait()
    print("Event set, joining thread...", flush=True)
    t.join()
    print("Thread joined, is_alive:", t.is_alive(), flush=True)

def test_batch4():
    print("\n--- Batch 4: Signals & OS ---", flush=True)
    print("SIGINT:", signal.SIGINT, flush=True)
    print("SIGTERM:", signal.SIGTERM, flush=True)
    old = signal.signal(signal.SIGINT, signal.SIG_DFL)
    print("Old SIGINT handler:", old, flush=True)
    
    p1, p2 = os.pipe()
    print("Pipe FDs:", p1, p2, flush=True)
    os.kill(os.getpid(), 0)
    print("os.kill(self, 0) - Success", flush=True)

def test_batch5():
    print("\n--- Batch 5: Inspection ---", flush=True)
    import inspect
    print("inspect.isfunction(test_batch1):", inspect.isfunction(test_batch1), flush=True)
    print("dir() count:", len(dir()), flush=True)

if __name__ == "__main__":
    test_batch1()
    test_batch2_3()
    test_batch4()
    test_batch5()
    print("\nAll batches verified.", flush=True)
