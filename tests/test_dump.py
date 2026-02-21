def f():
    d = {'a': 1, 'b': 2}
    return ((v, k) for k, v in d.items())
