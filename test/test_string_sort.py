# test_string_sort.py
# Verifies lexicographical string comparison

strings = ["banana", "apple", "cherry", "applepie", "Apple", "123", "!!!", "ñandú", "alpha"]
sorted_strings = sorted(strings)

print("Original:", strings)
print("Sorted:  ", sorted_strings)

expected = ["!!!", "123", "Apple", "alpha", "apple", "applepie", "banana", "cherry", "ñandú"]

if sorted_strings == expected:
    print("SUCCESS: Lexicographical sort verified.")
else:
    print("FAILURE: Sort order mismatch.")
    print("Expected:", expected)
    exit(1)
