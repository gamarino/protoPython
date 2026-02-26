def test_tuple_concat():
    a = (1, 2, 3)
    b = (4, 5, 6)
    for _ in range(100):
        a = a + b
    print("Done")

test_tuple_concat()
