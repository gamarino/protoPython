strings = ["banana", "apple", "cherry", "applepie", "ábol", "123", "Banana"]
sorted_strings = sorted(strings)
print("Original:", strings)
print("Sorted  :", sorted_strings)

expected = ["123", "Banana", "apple", "applepie", "banana", "cherry", "ábol"]
if sorted_strings == expected:
    print("SUCCESS: Strings sorted correctly")
else:
    print("FAILURE: Expected", expected)

# Test with inline strings (short) vs heap strings (long)
# "apple" is inline (5 chars), long_apple will be heap-based
long_apple = "apple" + "!" * 10
test_list = ["apple!", long_apple, "apple", "ap"]
sorted_test = sorted(test_list)
print("Test list  :", test_list)
print("Sorted test:", sorted_test)

expected_test = ["ap", "apple", "apple!", long_apple]
if sorted_test == expected_test:
    print("SUCCESS: Mixed string types sorted correctly")
else:
    print("FAILURE: Mixed types expected", expected_test)
