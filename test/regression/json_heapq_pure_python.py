# json and heapq use their complete pure-Python implementations. The native
# JsonModule and HeapqModule were removed: JsonModule copied escapes literally,
# rejected exponents and large integers and dumped floats with six significant
# digits; HeapqModule's heappush/heappop did not modify the caller's list.
# Neither _json nor _heapq is provided, so the stdlib fallbacks are used.
import heapq
import json

assert json.loads('"a\\nb\\u00e9"') == "a\nbé"
assert json.loads("123456789012345678901234567890") == 123456789012345678901234567890
assert json.loads("1.5e3") == 1500.0
assert json.dumps(0.1) == "0.1"
assert json.dumps([1, (2, 3), None, True]) == "[1, [2, 3], null, true]"
assert json.dumps({"b": 1, "a": [2.5]}, sort_keys=True) == '{"a": [2.5], "b": 1}'
assert json.loads(json.dumps({"k": ["x", 1, 2.25, None]})) == {"k": ["x", 1, 2.25, None]}

h = []
for v in (5, 1, 4, 2, 3):
    heapq.heappush(h, v)
assert h[0] == 1 and len(h) == 5
assert [heapq.heappop(h) for _ in range(5)] == [1, 2, 3, 4, 5] and h == []
data = [9, 7, 8, 1, 3]
heapq.heapify(data)
assert data[0] == 1
assert heapq.heapreplace(data, 6) == 1 and data[0] == 3
assert heapq.heappushpop(data, 0) == 0
assert heapq.nsmallest(2, [4, 1, 3, 2]) == [1, 2]
assert heapq.nlargest(2, [4, 1, 3, 2]) == [4, 3]

for name in ("_json", "_heapq"):
    try:
        __import__(name)
    except ImportError:
        pass
    else:
        raise AssertionError(name + " must not be importable")

print("OK")
