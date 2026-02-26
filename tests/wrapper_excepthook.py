import sys
import traceback

def hook(exc_type, exc_value, exc_traceback):
    print("----- CAUGHT BY HOOK -----")
    traceback.print_tb(exc_traceback)
    print(exc_type.__name__ + ":", exc_value)

sys.excepthook = hook

import test.cpython.test_generators
