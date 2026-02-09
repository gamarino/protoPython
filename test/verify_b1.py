import os
import _thread
import signal
import time

print("--- Batch 1: Builtins & Reflection ---", flush=True)
g = globals()
print("globals() type:", type(g), flush=True)
print("globals() len:", len(g), flush=True)
l = locals()
print("locals() is globals():", l is g, flush=True)
print("os.environ['TEST_VAR'] = 'Batch1'", flush=True)
os.environ['TEST_VAR'] = 'Batch1'
print("os.getenv('TEST_VAR'):", os.getenv('TEST_VAR'), flush=True)
print("Batch 1 Done", flush=True)
