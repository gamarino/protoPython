print("Step 1: Imports", flush=True)
import os, _thread, threading, signal, time
print("Step 2: globals()", flush=True)
g = globals()
print("Step 3: test functions", flush=True)
def test1():
    print("In test1", flush=True)
test1()
print("SUCCESS", flush=True)
