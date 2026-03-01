try:
    print(repr(str))
    print(type(str))
    s = str(5)
    print(repr(s))
except Exception as e:
    print(f"Exception: {e}")
