"""Phase 1 reproducer - DEBUG stdout cleanliness.

Importing a local module currently emits ~240 lines of "DEBUG:"
output on stdout (not stderr), pollluting the stdout of any
script that imports anything.  This script triggers the leak via
a benign local import and prints a marker.  Driver checks: stdout
must contain MARKER_PHASE1 AND must NOT contain any "DEBUG:" lines.
"""
import os
print("MARKER_PHASE1", flush=True)
