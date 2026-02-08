# test_string_methods.py
import sys

def assert_eq(a, b, msg=""):
    if a != b:
        print(f"FAIL: {a} != {b} | {msg}")
        sys.exit(1)
    else:
        print(f"PASS: {a} == {b} | {msg}")

# join
assert_eq(",".join(["a", "b", "c"]), "a,b,c", "join simple")
try:
    ",".join(["a", 1])
    print("FAIL: join did not raise TypeError for int")
    sys.exit(1)
except TypeError:
    print("PASS: join raised TypeError for int")

# split
assert_eq("a b  c".split(), ["a", "b", "c"], "split whitespace")
assert_eq("a,b,c".split(","), ["a", "b", "c"], "split separator")
assert_eq("a b c".split(None, 1), ["a", "b c"], "split maxsplit whitespace")
try:
    "a".split("")
    print("FAIL: split did not raise ValueError for empty sep")
    sys.exit(1)
except ValueError:
    print("PASS: split raised ValueError for empty sep")

# strip
assert_eq("  abc  ".strip(), "abc", "strip default")
assert_eq("...abc...".strip("."), "abc", "strip dots")

# find / index
assert_eq("abcabc".find("bc"), 1, "find simple")
assert_eq("abcabc".find("bc", 2), 4, "find start")
assert_eq("abcabc".find("bc", 2, 4), -1, "find start/end")

# startswith / endswith
assert_eq("abc".startswith("a"), True, "startswith simple")
assert_eq("abc".startswith("b", 1), True, "startswith offset")
assert_eq("abc".startswith(("x", "a")), True, "startswith tuple")
assert_eq("abc".endswith(("c", "y")), True, "endswith tuple")

# replace
assert_eq("aaa".replace("a", "b", 2), "bba", "replace count")
assert_eq("ab".replace("", "x"), "xaxbx", "replace empty")

print("ALL STRING TESTS PASSED")
sys.exit(0)
