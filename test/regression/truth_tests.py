# Truth testing of every built-in shape.
#
# Three code paths have to agree: `bool()`, the interpreter's own conditional
# jump (`if` / `while`), and PythonEnvironment::isTrue, the native truth test
# that protopyc-compiled code and several native modules call.
#
# Two defects this pins down:
#   * a str was measured through the list path rather than as a string;
#   * an integer too wide for 64 bits was converted with asLong, which raises
#     "LargeInteger value exceeds long long range" — so `while big_int:` failed
#     even though such a value is never zero.

CASES = [
    # (value, expected)
    (None, False),
    (True, True),
    (False, False),
    (0, False),
    (1, True),
    (-1, True),
    (0.0, False),
    (1.5, True),
    (-0.5, True),
    ("", False),
    ("a", True),
    ("0", True),
    ("      ", True),
    ("long enough to be a rope rather than an inline string", True),
    (b"", False),
    (b"x", True),
    ([], False),
    ([0], True),
    ((), False),
    ((0,), True),
    ({}, False),
    ({"k": 0}, True),
    (set(), False),
    ({0}, True),
    (frozenset(), False),
    (frozenset([0]), True),
]

for value, expected in CASES:
    # The messages use repr(), not %-formatting: "%d" % (2 ** 200) raises,
    # so formatting a large integer would hide the failure it reports.
    assert bool(value) is expected, "bool(" + repr(value) + ") should be " + repr(expected)
    if value:
        assert expected, "if accepted a false " + repr(value)
    else:
        assert not expected, "if rejected a true " + repr(value)
    assert (not value) is (not expected), "not " + repr(value)


# Integers wider than 64 bits are true, and testing them must not raise.
BIG = 2 ** 200
assert bool(BIG) is True
assert bool(-BIG) is True
assert bool(BIG - BIG) is False

if BIG:
    pass
else:
    raise AssertionError("a large integer tested as false")

# The loop form is the one that failed: `while a:` over a shrinking big int.
# The shift is written `a = a >> 32`, not `a >>= 32`: the augmented form has a
# separate in-place path that still converts through a 64-bit integer and
# raises on a LargeInteger. That is a distinct defect, not this one.
a = BIG
steps = 0
while a:
    a = a >> 32
    steps += 1
assert steps == 7, steps
assert a == 0

# The same through a comparison-free conditional expression.
assert (1 if BIG else 0) == 1

# 2**63 and 2**64 sit either side of the 64-bit boundary.
for value in (2 ** 62, 2 ** 63, 2 ** 64, 2 ** 63 - 1, 2 ** 63 + 1, -(2 ** 63), -(2 ** 64)):
    assert bool(value) is True, value
    if not value:
        raise AssertionError("%d tested as false" % value)


# __bool__ wins over __len__.
class BoolFalseLenTwo:
    def __bool__(self):
        return False

    def __len__(self):
        return 2


class BoolTrueLenZero:
    def __bool__(self):
        return True

    def __len__(self):
        return 0


class LenOnlyZero:
    def __len__(self):
        return 0


class LenOnlyThree:
    def __len__(self):
        return 3


class Neither:
    pass


assert bool(BoolFalseLenTwo()) is False
assert bool(BoolTrueLenZero()) is True
assert bool(LenOnlyZero()) is False
assert bool(LenOnlyThree()) is True
assert bool(Neither()) is True

for obj, expected in ((BoolFalseLenTwo(), False), (BoolTrueLenZero(), True),
                      (LenOnlyZero(), False), (LenOnlyThree(), True),
                      (Neither(), True)):
    if obj:
        assert expected, "if accepted a false %s" % type(obj).__name__
    else:
        assert not expected, "if rejected a true %s" % type(obj).__name__


# str and bytes subclasses follow their contents.
class MyStr(str):
    pass


class MyBytes(bytes):
    pass


assert bool(MyStr("")) is False
assert bool(MyStr("x")) is True
assert bool(MyBytes(b"")) is False
assert bool(MyBytes(b"x")) is True

# Strings either side of the inline-storage boundary (6 UTF-8 bytes).
for text in ("", "a", "ab", "abcde", "abcdef", "abcdefg", "abcdefghij" * 10):
    assert bool(text) is (len(text) > 0)
    if text:
        assert len(text) > 0
    else:
        assert len(text) == 0

# Native truth tests: os.set_inheritable takes its flag through isTrue, the
# native truth test. It has to agree with bool() on every shape, including
# user-defined objects: the container checks used to run before the dunder
# lookups, and one of them matches an ordinary instance's own-attribute
# storage, so every user-defined object tested false.
import os


class PlainInstance:
    pass


class LenThree:
    def __len__(self):
        return 3


class LenZero:
    def __len__(self):
        return 0


NATIVE_CASES = [
    (True, True), (False, False), (1, True), (0, False),
    ("x", True), ("", False), ([1], True), ([], False),
    ({}, False), ({"a": 1}, True),
    (set(), False), ({1}, True),
    (frozenset(), False), (frozenset([1]), True),
    ((), False), ((1,), True),
    (2 ** 200, True),
    (PlainInstance(), True),
    (LenThree(), True),
    (LenZero(), False),
]

r, w = os.pipe()
try:
    for flag, expected in NATIVE_CASES:
        os.set_inheritable(r, flag)
        got = os.get_inheritable(r)
        assert got is expected, \
            "native truth test of " + repr(flag) + " gave " + repr(got)
        # And it agrees with bool().
        assert got is bool(flag), "native truth test disagrees with bool() for " + repr(flag)
finally:
    os.close(r)
    os.close(w)

print("truth_tests: ok")
