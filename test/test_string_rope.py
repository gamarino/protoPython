# test_string_rope.py
# Verifies lexicographical string comparison for rope structures

def make_rope(s):
    if not s: return ""
    if len(s) == 1: return s
    mid = len(s) // 2
    return make_rope(s[:mid]) + make_rope(s[mid:])

strings = ["banana", "apple", "cherry", "applepie", "Apple", "123", "!!!", "ñandú", "alpha"]
ropes = [make_rope(s) for s in strings]

sorted_ropes = sorted(ropes)

print("Original Ropes:", ropes)
print("Sorted Ropes:  ", sorted_ropes)

expected = ["!!!", "123", "Apple", "alpha", "apple", "applepie", "banana", "cherry", "ñandú"]

if sorted_ropes == expected:
    print("SUCCESS: Rope lexicographical sort verified.")
else:
    print("FAILURE: Rope sort order mismatch.")
    print("Expected:", expected)
    exit(1)
