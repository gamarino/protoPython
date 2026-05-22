import sys, traceback
import unittest.loader
sys.settrace(None)

class Hook(unittest.TestLoader):
    pass

tl = Hook()
# Try to trigger the setitem
try:
    tl['key'] = 'val'
except Exception as e:
    print(f"Direct: {type(e).__name__}: {e}")
    traceback.print_exc()
