import sys
import time
try:
    s = time.strftime("%Y-%m-%d")
    print(f"PRE-IMPORT time.strftime result: {s}")
    from test import support
    print("SUCCESS: test.support imported")
    s2 = time.strftime("%Y-%m-%d")
    print(f"POST-IMPORT time.strftime result: {s2}")
except Exception as e:
    print(f"FAILURE: {e}")
    import traceback
    traceback.print_exc()
