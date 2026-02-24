def f():
    g = globals()
    print("type(globals()):", type(g))
    print("type(locals()):", type(locals()))
f()
