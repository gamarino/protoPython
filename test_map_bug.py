def f(x): return x.upper()
l = ['a', 'b']
try:
    m = map(f, l)
    print("MAP OBJ:", m)
    if m is None:
        print("MAP IS NONE!")
    else:
        res = list(m)
        print("LIST(M):", res)
        if res == ['A', 'B']:
            print("MAP OK")
        else:
            print("MAP FAILED:", res)
except Exception as e:
    import traceback
    traceback.print_exc()
    print("MAP EXCEPTION:", e)
