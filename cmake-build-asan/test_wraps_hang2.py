def wraps(wrapped, assigned=('__module__', '__name__', '__qualname__', '__doc__', '__annotate__'), updated=('__dict__',)):
    def wrapper(f):
        print("wraps wrapper called")
        for attr in assigned:
            print("loop start:", attr)
            try:
                print("getting", attr)
                value = getattr(wrapped, attr)
                print("setting", attr)
                setattr(f, attr, value)
                print("done setting", attr)
            except AttributeError as e:
                print("AttributeError on", attr, type(e))
                pass
            except Exception as e:
                print("Other Exception on", attr, type(e))
            print("loop end:", attr)
        for attr in updated:
            print("updating", attr)
            try:
                getattr(f, attr).update(getattr(wrapped, attr, {}))
            except Exception as e:
                print("Exception on updating", attr, type(e))
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
