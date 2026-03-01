try:
    d1 = {'a': 1, 'b': 2}
    d2 = dict(d1, dest="my_dest")
    print("d2 is dict:", d2 is dict)
    print("d2 type:", type(d2))
except Exception as e:
    print(f"Exception: {e}")
