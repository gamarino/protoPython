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
