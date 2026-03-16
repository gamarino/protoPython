def test():
    items = {"A": 1, "B": 2, "C": 3}
    keys = ["A", "B", "C"]
    gen = (items[k] for k in keys)
    print("Gen list:", list(gen))

test()
