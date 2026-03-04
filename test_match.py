try:
    raise ImportError
except ImportError:
    print("MATCHED")
except Exception:
    print("MATCHED EXCEPTION")
print("DONE")
