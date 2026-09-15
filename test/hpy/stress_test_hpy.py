# stress_test_hpy.py
# Imports an HPy extension module and calls one of its functions 1000 times,
# exercising module lookup, the method wrapper and HPy handle recycling.
# Run with the directory holding math_hpy.hpy.so on the module search path.

import math_hpy
import sys

print("Starting HPy stress test...")

for i in range(1000):
    # In protoPython, the provider caches handles, so this tests the resolution
    # and retrieval path from the provider cache.
    m = __import__("math_hpy")
    if m.add(i, i) != i*2:
        print(f"Error at iteration {i}")
        sys.exit(1)

print("HPy stress test passed: 1000 iterations.")
