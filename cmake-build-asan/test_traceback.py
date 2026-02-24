import traceback
import sys

try:
    import functools
    print("Success imported functools")
except Exception as e:
    print("Exception during import:")
    traceback.print_exc()
