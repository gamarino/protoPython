try:
    import sys
    print("sys.path:", sys.path)
    print("Attempting to import inspect...")
    import inspect
    print("SUCCESS: inspect imported")
    print("inspect.isfunction(print):", inspect.isfunction(print))
except ImportError as e:
    print("FAILURE: ImportError:", e)
except Exception as e:
    print("FAILURE: Exception:", type(e).__name__, ":", e)
    import traceback
    traceback.print_exc()
