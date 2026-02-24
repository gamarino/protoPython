try:
    fs = frozenset({'__await__'})
    print("fs:", fs)
    iter_fs = iter(fs)
    print("iter:", iter_fs)
    print("next1:", next(iter_fs, "EOF"))
    print("next2:", next(iter_fs, "EOF"))
    print("next3:", next(iter_fs, "EOF"))
except Exception as e:
    print("Error:", e)
