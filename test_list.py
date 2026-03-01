try:
    l1 = list([1, 2, 3])
    print("l1:", l1)
    l2 = [1]
    l3 = l2([1, 2, 3])
    print("l3:", l3)
except Exception as e:
    print(f"Exception: {e}")
