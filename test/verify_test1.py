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

def test_batch2_3():
    print("\n--- Batch 2 & 3: Concurrency ---")
test_batch1()
