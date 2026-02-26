from collections import namedtuple
print("import successful")
try:
    T = namedtuple("T", "failed attempted")
    print("namedtuple created successfully")
except Exception as e:
    print(f"Exception: {type(e).__name__}: {e}")
