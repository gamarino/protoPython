try:
    print("Calling None...")
    res = None()
    print(f"Result of None(): {res}")
except TypeError as e:
    print(f"Caught expected TypeError: {e}")
except Exception as e:
    print(f"Caught unexpected exception: {type(e)}: {e}")

item = None()
print(f"item is None: {item is None}")
