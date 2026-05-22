import test_descr, traceback
pt = test_descr.PicklingTests()
for tname in ['test_pickle_slots', 'test_reduce_copying']:
    try:
        m = getattr(pt, tname)
        m()
        print(f"PASS: {tname}")
    except Exception as e:
        s = str(e); 
        if len(s)>100: s=s[:100]
        print(f"FAIL {tname}: {type(e).__name__}: {s}")
