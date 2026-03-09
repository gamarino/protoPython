import json
try:
    data = '{"a": [1, 2, 3], "b": 42}'
    res = json.loads(data)
    print("JSON RES:", res)
    if res.get("a") == [1, 2, 3]:
        print("JSON LIST OK")
    else:
        print("JSON LIST EMPTY or MISMATCH:", res.get("a"))
except Exception as e:
    import traceback
    traceback.print_exc()
    print("JSON FAILED:", e)
