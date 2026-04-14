import os
print("Importing os...")
try:
    print("Environ type:", type(os.environ))
    print("Environ keys:", list(os.environ.keys()))
except Exception as e:
    import traceback
    traceback.print_exc()
