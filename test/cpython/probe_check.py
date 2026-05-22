import test_descr
cpm = test_descr.ClassPropertiesAndMethods()
pt = test_descr.PicklingTests()
mt = test_descr.MroTest()
for grp,tname in [(cpm, 'test_funny_new'), (cpm, 'test_metaclass'),
                  (pt, 'test_pickle_slots'), (pt, 'test_reduce_copying'),
                  (cpm, 'test_proxy_call')]:
    try:
        m = getattr(grp, tname)
        m()
        print(f"PASS: {tname}")
    except Exception as e:
        s = str(e)
        if len(s) > 100: s = s[:100]
        print(f"FAIL {tname}: {type(e).__name__}: {s}")
