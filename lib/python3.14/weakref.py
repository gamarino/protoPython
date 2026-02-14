# weakref.py - Minimal stub for CPython conformance tests.

class ref:
    def __init__(self, obj, callback=None):
        self.obj = obj
    def __call__(self):
        return self.obj

def proxy(obj, callback=None):
    return obj
