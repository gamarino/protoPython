try:
    s = str(5)
    print("str(5):", repr(s))
    i = int("5")
    print("int('5'):", repr(i))
except Exception as e:
    print(f"Exception: {e}")
