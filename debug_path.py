import sys
print(f"sys.path: {sys.path}")
try:
    import test.support
    print("test.support imported successfully")
except ImportError as e:
    print(f"ImportError: {e}")

try:
    import unittest
    print("unittest imported successfully")
except ImportError as e:
    print(f"ImportError: {e}")
