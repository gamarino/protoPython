# To test real _collections_abc.py, we need to hide the native one or ensure it's not used.
# But for now, let's just see if we can load it if we rename it or something.
# Or better: let's see if we can load the file content and execute it.
import sys
import os

print("Testing loading of real _collections_abc.py")
try:
    with open("lib/python3.14/_collections_abc.py", "r") as f:
        src = f.read()
    print("Read _collections_abc.py, length:", len(src))
    # We execute it in a new namespace to avoid mess
    ns = {}
    exec(src, ns)
    print("Successfully executed _collections_abc.py")
    Mapping = ns.get("Mapping")
    print("Mapping:", Mapping)
    print("type(Mapping):", type(Mapping))
    print("Mapping.__bases__:", Mapping.__bases__)
except Exception as e:
    print("Error loading _collections_abc.py:", e)
    import traceback
    traceback.print_exc()
