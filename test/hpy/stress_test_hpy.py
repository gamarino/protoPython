# stress_test_hpy.py
# Imports an HPy extension module and calls one of its functions many times,
# exercising module lookup, the method wrapper and HPy handle recycling.
# Run with the directory holding math_hpy.hpy.so on the module search path.
#
# The iteration count matters: CMakeLists.txt runs this test under
# PROTOCORE_HEAP_LIMIT_CELLS so that the collector runs while handles are being
# created and recycled, and protoCore only starts a cycle when a thread needs
# cells the heap cannot supply below the ceiling. A short run allocates less
# than the heap it already holds and never collects, whatever the ceiling is.

import math_hpy
import sys

ITERATIONS = 20000

print("Starting HPy stress test...")

for i in range(ITERATIONS):
    # In protoPython, the provider caches handles, so this tests the resolution
    # and retrieval path from the provider cache.
    m = __import__("math_hpy")
    if m.add(i, i) != i*2:
        print(f"Error at iteration {i}")
        sys.exit(1)

print(f"HPy stress test passed: {ITERATIONS} iterations.")
