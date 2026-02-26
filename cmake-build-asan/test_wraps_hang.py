def wraps(wrapped, assigned=('__module__', '__name__', '__qualname__', '__doc__', '__annotate__'), updated=('__dict__',)):
    def wrapper(f):
        print("wraps wrapper called")
        for attr in assigned:
            try:
                print("getting", attr)
                value = getattr(wrapped, attr)
                print("setting", attr)
                setattr(f, attr, value)
            except AttributeError:
                pass
        for attr in updated:
            print("updating", attr)
            getattr(f, attr).update(getattr(wrapped, attr, {}))
            print("updated", attr)
        f.__wrapped__ = wrapped
        return f
    return wrapper

def reduce(*args):
    pass

def _warn_python_reduce_kwargs(py_reduce):
    print("Inside _warn")
    @wraps(py_reduce)
    def wrapper(*args, **kwargs):
        pass
    print("Returning wrapper")
    return wrapper

print("Before reduce assignment")
reduce = _warn_python_reduce_kwargs(reduce)
print("After reduce assignment")
