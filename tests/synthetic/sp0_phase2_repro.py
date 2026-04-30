"""Phase 2 reproducer — json.dumps must encode integers correctly.

Currently json.dumps(5) returns "<NoneType object at (nil)>" instead
of "5"; strings work fine.  This reproducer asserts canonical JSON
encoding for several int-bearing shapes.
"""
import json

assert json.dumps(5) == "5", f"int dumps: {json.dumps(5)!r}"
assert json.dumps({"k": 1}) == '{"k": 1}', \
    f"dict dumps: {json.dumps({'k': 1})!r}"
assert json.dumps([2, 3, 4]) == "[2, 3, 4]", \
    f"list dumps: {json.dumps([2, 3, 4])!r}"
assert json.dumps({"int": 1, "list": [2, 3], "nested": {"x": 4}}) \
    == '{"int": 1, "list": [2, 3], "nested": {"x": 4}}'
print("PHASE2_OK")
