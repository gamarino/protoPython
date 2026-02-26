
try:
    import sys
    print("Accessing sys.missing_attr...")
    print(sys.missing_attr)
    print("ERROR: sys.missing_attr did not raise AttributeError and returned:", sys.missing_attr)
except AttributeError:
    print("SUCCESS: AttributeError raised for sys.missing_attr")
except Exception as e:
    print(f"ERROR: Raised wrong exception: {type(e).__name__}: {e}")

try:
    import _collections
    print("Accessing _collections._tuplegetter...")
    val = _collections._tuplegetter
    print(f"ERROR: _collections._tuplegetter did not raise AttributeError and returned: {val}")
except AttributeError:
    print("SUCCESS: AttributeError raised for _collections._tuplegetter")
except Exception as e:
    print(f"ERROR: Raised wrong exception for _collections: {type(e).__name__}: {e}")
