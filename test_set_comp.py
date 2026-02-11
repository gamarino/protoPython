# test_set_comp.py
print("Testing set comprehensions...")
s = {i for i in range(5)}
print("s:", s)
# Note: set order might not be guaranteed, but for 0-4 hashing might be stable.
# Let's just check length and contains.
assert len(s) == 5
for i in range(5):
    assert i in s

filtered = {x for x in range(10) if x % 2 == 0}
print("filtered set:", filtered)
assert len(filtered) == 5
for i in range(0, 10, 2):
    assert i in filtered

print("Set comprehensions passed")
