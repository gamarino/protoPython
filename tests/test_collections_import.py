print("Starting import diag")
try:
    import collections
    print("Successfully imported collections")
except Exception as e:
    print("Caught error during import collections:", type(e), e)
    import traceback
    # traceback might not be available yet if basic stuff is broken
    try:
        # traceback.print_exc()
        pass
    except:
        pass

print("\nChecking type() again")
try:
    T = type("T", (), {})
    print("type() works:", T)
except Exception as e:
    print("type() failed:", e)

print("\nChecking if _collections exists in sys.modules")
try:
    import sys
    print("_collections in sys.modules:", "_collections" in sys.modules)
    print("collections in sys.modules:", "collections" in sys.modules)
except:
    pass
