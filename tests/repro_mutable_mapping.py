from _collections_abc import MutableMapping
import os

print("MutableMapping type:", type(MutableMapping))
print("os.environ type:", type(os.environ))

try:
    print("os.environ._data:", os.environ._data)
except Exception as e:
    print("Caught:", type(e).__name__, ":", e)

class M(MutableMapping):
    def __init__(self):
        self._data = {}
    def __getitem__(self, k): return self._data[k]
    def __setitem__(self, k, v): self._data[k] = v
    def __delitem__(self, k): del self._data[k]
    def __iter__(self): return iter(self._data)
    def __len__(self): return len(self._data)

m = M()
print("M instance type:", type(m))
print("m._data:", m._data)
print("repr(m):", repr(m))
