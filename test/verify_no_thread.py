import os
import _thread
import signal
import time

def test_batch1():
    print("--- Batch 1: Builtins & Reflection ---")
    g = globals()
    print("globals() type:", type(g))
    print("globals() len:", len(g))
    l = locals()
    print("locals() is globals():", l is g)
    v = vars(os)
    print("vars(os) type:", type(v))
    print("os.environ['TEST_VAR'] = 'Batch1'")
    os.environ['TEST_VAR'] = 'Batch1'
    print("os.getenv('TEST_VAR'):", os.getenv('TEST_VAR'))

def test_batch2_3():
    print("\n--- Batch 2 & 3: Concurrency ---")
    
    def worker():
        print("Worker starting...")
        with lock:
            print("Worker acquired lock")
        evt.set()
        print("Worker finished")

    print("Starting thread...")
    t.start()
    print("Thread started, is_alive:", t.is_alive())
    print("Waiting for event...")
    evt.wait()
    print("Event set, joining thread...")
    t.join()
    print("Thread joined, is_alive:", t.is_alive())

def test_batch4():
    print("\n--- Batch 4: Signals & OS ---")
    print("SIGINT:", signal.SIGINT)
    print("SIGTERM:", signal.SIGTERM)
    old = signal.signal(signal.SIGINT, signal.SIG_DFL)
    print("Old SIGINT handler:", old)
    
    p1, p2 = os.pipe()
    print("Pipe FDs:", p1, p2)
    os.kill(os.getpid(), 0) # Just check if it exists
    print("os.kill(self, 0) - Success")

def test_batch5():
    print("\n--- Batch 5: Inspection ---")
    import inspect
    print("inspect.isfunction(test_batch1):", inspect.isfunction(test_batch1))
    print("dir() count:", len(dir()))
    # help(os) # This would be too verbose for automated test

if __name__ == "__main__":
    test_batch1()
    test_batch2_3()
    test_batch4()
    test_batch5()
    print("\nAll batches verified.")
