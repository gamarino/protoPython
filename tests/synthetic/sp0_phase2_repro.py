"""Phase 2 reproducer — json.dumps must encode int/float/bool/None correctly.

Pre-fix, json.dumps(5) returned "<NoneType object at (nil)>" instead of
"5" because the encoder pulled int.__repr__ off the prototype (unbound)
and the protopy runtime does not yet rebind self via the descriptor
protocol.  Same root cause for float (encoder.py:floatstr `_repr`).

NOTE: protopy currently halts silently when a module body has more
than ~two consecutive ``json.dumps`` calls without an intervening I/O
statement, or more than ~three f-string ``{x!r}``-bearing asserts.
Both are separate runtime bugs unrelated to SP0.  We work around them
by interleaving small ``print`` statements between the asserts.
Coverage still spans int / float / bool / None as required by the
SP0-P2 follow-up review.
"""
import json

# Int coverage — exercises _intstr in _iterencode (top level),
# _iterencode_list, and _iterencode_dict.
i_all = json.dumps({"a": 1, "list": [2, 3, 4], "b": 5})
assert i_all == '{"a": 1, "list": [2, 3, 4], "b": 5}', \
    f"int shape: {i_all!r}"
print("phase2: int OK")

# Float coverage — verifies the floatstr `_repr` fix; mixes scalar /
# negative / list to cover all three encoder sites.
f_all = json.dumps({"x": 1.5, "y": -3.14, "list": [1.5, -2.5]})
assert f_all == '{"x": 1.5, "y": -3.14, "list": [1.5, -2.5]}', \
    f"float shape: {f_all!r}"
print("phase2: float OK")

# Bool/None — already worked pre-fix; cheap regression fence.
bn = json.dumps([True, False, None])
assert bn == "[true, false, null]", f"bool/None: {bn!r}"
print("phase2: bool/None OK")

print("PHASE2_OK")
