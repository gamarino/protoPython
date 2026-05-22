import test_descr
t = test_descr.ClassPropertiesAndMethods()
try: t.test_gh55664()
except Exception as e:
    msg=str(e); 
    if len(msg)>150: msg=msg[:150]
    print(f"FAIL: {type(e).__name__}: {msg}")
