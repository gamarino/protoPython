import sys
import _collections
from _collections import deque

print("--- sys.path check ---")
print("sys.path type:", type(sys.path))
print("sys.path value:", sys.path)

print("--- deque check ---")
d = deque([1, 2, 3])
print("deque type:", type(d))
print("deque value:", d)

print("--- mutation check ---")
try:
    for x in d:
        print("Iterating:", x)
        d.append(4)
except RuntimeError as e:
    print("Caught expected RuntimeError:", e)

print("Success!")
