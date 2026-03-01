try:
    d = {'a': 1}
    print("d is callable?", callable(d))
    d2 = d({'b': 2})
    print("d2:", d2)
except Exception as e:
    print(f"Exception: {e}")
