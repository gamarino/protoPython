"""Phase 2.5 reproducer — silent halt at 4+ consecutive module-level
dumps/asserts.  Before the fix, this script had ~17% chance of silent
exit 0 with no output.  After the fix, it must run reliably 30/30 runs."""
import json
a = json.dumps(5)
b = json.dumps({"k": 1})
c = json.dumps([2, 3, 4])
d = json.dumps({"int": 1, "list": [2, 3], "nested": {"x": 4}})
assert a == "5"
assert b == '{"k": 1}'
assert c == "[2, 3, 4]"
assert d == '{"int": 1, "list": [2, 3], "nested": {"x": 4}}'
print("PHASE2_5_OK")
