def test():
    _globals = {}
    _have_functions = ["HAVE_FACCESSAT"]
    fn = "access"
    str = "HAVE_FACCESSAT"
    if (fn in _globals) and (str in _have_functions):
        print(_globals[fn])
    else:
        print("Short-circuited correctly!")

test()
