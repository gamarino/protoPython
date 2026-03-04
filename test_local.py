import threading
try:
    print("creating local")
    l = threading.local()
    print("created local", l)
except Exception as e:
    import traceback
    traceback.print_exc()
