# test_dict_comp.py
print("Testing dictionary comprehensions...")
d = {i: i*2 for i in range(5)}
print("d:", d)
assert len(d) == 5
for i in range(5):
    assert d[i] == i*2

# Nested comprehension
nested = {(i, j): i + j for i in range(2) for j in range(2)}
print("nested:", nested)
assert len(nested) == 4
for i in range(2):
    for j in range(2):
        assert nested[(i, j)] == i + j

# Conditional
filtered = {k: v for k, v in d.items() if k % 2 == 0}
print("filtered:", filtered)
assert len(filtered) == 3

print("Dictionary comprehensions passed")
