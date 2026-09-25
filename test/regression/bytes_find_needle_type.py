"""bytes.find / rfind / index / rindex / count reject a needle that is neither an
integer nor bytes-like, with CPython's TypeError.

The defect this pins down was not dead code.  py_bytes_find guarded the case with
`sub->getAttribute(context, "__data__") == nullptr`, and protoCore's getAttribute
answers PROTO_NONE for an absent attribute and nullptr only for invalid input, so
the comparison was always false and the guard never ran: b"abc".find(W()) fell
through to a search for the empty needle and answered 0.  CPython raises
TypeError.

Mutation that must turn this file red again: make bytes_needle_validate return
true for a value that bytes_view cannot read (drop its final TypeError branch, or
restore the `== nullptr` comparison in py_bytes_find).  find() then answers 0
again and the first assertion fails.
"""

NAMES = ("find", "rfind", "index", "rindex", "count")


def type_error_message(method, needle):
    """The TypeError message `method(needle)` raises, or None when it raises none."""
    try:
        method(needle)
    except TypeError as exc:
        return str(exc)
    return None


class W:
    pass


# --- the defect itself -----------------------------------------------------
for name in NAMES:
    method = getattr(b"abc", name)
    msg = type_error_message(method, W())
    assert msg is not None, name + " accepted a non-bytes-like needle"
    assert msg == "argument should be integer or bytes-like object, not 'W'", name + ": " + msg

# A plain str was already rejected; it must stay rejected, under its own name.
for name in NAMES:
    method = getattr(b"abc", name)
    msg = type_error_message(method, "b")
    assert msg == "argument should be integer or bytes-like object, not 'str'", name + ": " + str(msg)

# None and a list are ordinary non-bytes-like objects too.
assert type_error_message(b"abc".find, None) is not None
assert type_error_message(b"abc".find, [98]) is not None

# --- everything that must keep working ------------------------------------
assert b"abc".find(98) == 1 and b"abc".rfind(99) == 2 and b"abc".count(97) == 1
assert b"abcabc".find(b"bc") == 1 and b"abcabc".rfind(b"bc") == 4
assert b"abcabc".count(b"bc") == 2 and b"abcabc".index(b"bc") == 1
assert b"abcabc".find(bytearray(b"ca")) == 2
assert b"abcabc".find(memoryview(b"ca")) == 2
assert b"abc".find(b"") == 0
assert b"abc".find(b"z") == -1 and b"abc".rfind(b"z") == -1 and b"abc".count(b"z") == 0
assert b"abcabc".find(b"bc", 2) == 4
# NOT asserted here, and deliberately: b"abcabc".find(b"bc", 0, 2) answers 1 where
# CPython answers -1, because py_bytes_find compares the match position against
# `end` instead of the match's END against `end`.  That is a separate defect,
# found while writing this file and filed in docs/CONFORMANCE.md rather than
# fixed here; asserting today's answer would freeze the bug into the suite.

# A bytes-like needle that is simply absent is a ValueError from index, never the
# TypeError this fix introduces.
try:
    b"abc".index(b"z")
    raise AssertionError("index of a missing needle must raise")
except TypeError:
    raise AssertionError("index of a missing bytes needle raised TypeError")
except ValueError:
    pass

print("bytes find needle type OK")
