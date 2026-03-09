try:
    fs = frozenset([1, 2, 3])
    print("FROZENSET:", fs)
    if 1 in fs:
        print("FROZENSET OK")
    else:
        print("FROZENSET EMPTY?")
except Exception as e:
    import traceback
    traceback.print_exc()
    print("FROZENSET FAILED:", e)

try:
    s = set([1, 2, 3])
    print("SET:", s)
except Exception as e:
    print("SET FAILED:", e)
