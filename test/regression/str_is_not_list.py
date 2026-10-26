# A str is not a list.  protoCore's ProtoObject::asList answers a string with
# a new list of its characters, and every type test written with it let a
# str through as a list: isinstance("abc", list) was True, [1] + "abc" and
# l.extend("abc") produced protoCore character cells, and s[0] = "x" was
# silently accepted.  Expected values are CPython 3's.


def raises(exc, fn, *args):
    try:
        fn(*args)
    except exc:
        return True
    return False


class S(str):
    pass


class L(list):
    pass


s = "abc"
big = "ab" * 50000

# isinstance / issubclass / type
for v in (s, "", big, S("xyz")):
    assert not isinstance(v, list), repr(v)[:20]
    assert not isinstance(v, (list, tuple, dict, set))
    assert isinstance(v, str)
    assert type(v) is not list
assert type(s) is str and type(big).__name__ == "str"
assert type(S("x")) is S
assert not issubclass(str, list) and not issubclass(S, list)
assert isinstance([], list) and isinstance(L(), list)
assert not isinstance(b"ab", list) and not isinstance(bytearray(b"ab"), list)
assert not isinstance((1, 2), list)

# conversions and iteration yield str characters
assert list(s) == ["a", "b", "c"]
assert list(S("xy")) == ["x", "y"]
assert tuple(s) == ("a", "b", "c")
assert sorted("cab") == ["a", "b", "c"]
assert list(reversed(s)) == ["c", "b", "a"]
assert dict([s[:2]]) == {"a": "b"}
assert list(enumerate("ab")) == [(0, "a"), (1, "b")]


def star(a, b, c):
    return (a, b, c)


assert star(*s) == ("a", "b", "c")
a, b, c = s
assert (a, b, c) == ("a", "b", "c")
first, *rest = s
assert first == "a" and rest == ["b", "c"]

# membership
assert "b" in s and "b" not in ["abc"] and "abc" in ["abc"]

# list operators and methods with a str operand
assert raises(TypeError, lambda: s + [1])
assert raises(TypeError, lambda: [1] + s)
l = [0]
l.extend(s)
assert l == [0, "a", "b", "c"]
l = []
alias = l
l += "ab"                      # module level: INPLACE_ADD
assert l == ["a", "b"] and alias is l


def in_function():
    m = []
    alias = m
    m += "ab"                  # function: the fused INC_FAST_K opcode
    assert m == ["a", "b"] and alias is m
    n = []
    k = "cd"
    alias = n
    n += k                     # function: the fused ACC_FAST_FAST opcode
    assert n == ["c", "d"] and alias is n
    o = [1]
    alias = o
    o += [2]
    assert o == [1, 2] and alias is o, "list += list must extend in place"


in_function()
assert [] != s and ["a", "b", "c"] != s and s != ["a", "b", "c"]
assert raises(TypeError, lambda: ["a"] < s)

# sum() uses the + protocol
assert raises(TypeError, sum, s)
assert raises(TypeError, sum, ["a"])
assert sum([[1], [2]], []) == [1, 2]

# a str is immutable
def setitem():
    t = "ab"
    t[0] = "x"


def setitem_tuple():
    t = (1, 2)
    t[0] = 3


assert raises(TypeError, setitem)
assert raises(TypeError, setitem_tuple)
assert raises(TypeError, __import__("random").shuffle, "ab")

# len() and truthiness
assert len(s) == 3 and len(S("xy")) == 2 and len(big) == 100000 and len("") == 0
assert bool("") is False and bool(s) is True

print("ok")
