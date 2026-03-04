import sys
print("sys.modules has _weakref:", "_weakref" in sys.modules)
if "_weakref" in sys.modules:
    print("Value:", sys.modules["_weakref"])

try:
    import _weakref
    print("SUCCESS")
except Exception as e:
    print("FAILED:", type(e), e)
