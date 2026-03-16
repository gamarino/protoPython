try:
    d1 = {'a': 1, 'b': 2}
    d2 = dict(d1, dest="my_dest")
    print(d2)
except Exception as e:
    print(f"Exception: {e}")
