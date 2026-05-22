import test_descr
t = test_descr.ClassPropertiesAndMethods()
try:
    t.test_metaclass()
    print("PASS")
except Exception as e:
    msg = str(e); 
    if len(msg) > 200: msg = msg[:200]
    print(f"FAIL: {type(e).__name__}: {msg}")
