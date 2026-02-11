# test_list_comp.py
print("Testing list comprehensions...")
lst = [i * 2 for i in range(5)]
print("lst:", lst)
assert lst == [0, 2, 4, 6, 8]

filtered = [i for i in range(10) if i % 2 == 0]
print("filtered:", filtered)
assert filtered == [0, 2, 4, 6, 8]

nested = [(x, y) for x in range(2) for y in range(2)]
print("nested:", nested)
assert nested == [(0, 0), (0, 1), (1, 0), (1, 1)]

print("List comprehensions passed")
