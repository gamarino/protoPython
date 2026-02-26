def _check_methods(C, *methods):
    mro = C
    for method in methods:
        for B in mro:
            if method == B:
                break
        else:
            return NotImplemented
    return True
test_mro = (1, 2, 3)
methods = (2, 3)
print(_check_methods(test_mro, *methods))
