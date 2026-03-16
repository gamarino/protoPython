try:
    import _weakref
    print("_weakref OK")
except Exception as e:
    import traceback; traceback.print_exc()

try:
    import weakref
    print("weakref OK")
except Exception as e:
    import traceback; traceback.print_exc()

try:
    import _thread
    print("_thread OK")
except Exception as e:
    import traceback; traceback.print_exc()

try:
    import threading
    print("threading OK")
except Exception as e:
    import traceback; traceback.print_exc()

try:
    import unittest
    print("unittest OK")
except Exception as e:
    import traceback; traceback.print_exc()
