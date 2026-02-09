print("IMPORT START", flush=True)
import os
print("IMPORT os DONE", flush=True)
import _thread
print("IMPORT _thread DONE", flush=True)
import threading
print("IMPORT threading DONE", flush=True)
import signal
print("IMPORT signal DONE", flush=True)
import time
print("IMPORT time DONE", flush=True)

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

# def test_batch2_3():
#     print("\n--- Batch 2 & 3: Concurrency ---")
#     lock = threading.Lock()
#     rlock = threading.RLock()
#     evt = threading.Event()
#     
#     def worker():
#         print("Worker starting...", flush=True)
#         with lock:
#             print("Worker acquired lock", flush=True)
#         evt.set()
#         print("Worker finished", flush=True)
# 
#     t = threading.Thread(target=worker)
#     print("Starting thread...", flush=True)
#     t.start()
#     print("Thread started, is_alive:", t.is_alive(), flush=True)
    print("\n--- Batch 5: Inspection ---")
    import inspect
    print("inspect.isfunction(test_batch1):", inspect.isfunction(test_batch1))
    print("dir() count:", len(dir()))
    # help(os) # This would be too verbose for automated test

if __name__ == "__main__":
