import sys

def wraps(wrapped, assigned=('__module__', '__name__', '__qualname__', '__doc__', '__annotate__'), updated=('__dict__',)):
    def wrapper(f):
        print("wraps wrapper called", flush=True)
        for attr in assigned:
            print("loop start:", attr, flush=True)
            try:
                print("getting", attr, flush=True)
                value = getattr(wrapped, attr)
                print("setting", attr, flush=True)
                setattr(f, attr, value)
                print("done setting", attr, flush=True)
            except AttributeError as e:
                print("AttributeError on", attr, type(e), flush=True)
                pass
            except Exception as e:
                print("Other Exception on", attr, type(e), flush=True)
            print("loop end:", attr, flush=True)
        
        print("Starting updated loop", flush=True)
        for attr in updated:
            print("updating", attr, flush=True)
            try:
                getattr(f, attr).update(getattr(wrapped, attr, {}))
            except Exception as e:
                print("Exception on updating", attr, type(e), flush=True)
            print("updated", attr, flush=True)
        print("Finishing updated loop", flush=True)
        f.__wrapped__ = wrapped
        return f
    return wrapper

def reduce(*args):
    pass

def _warn_python_reduce_kwargs(py_reduce):
    print("Inside _warn", flush=True)
    @wraps(py_reduce)
    def wrapper(*args, **kwargs):
        pass
    print("Returning wrapper", flush=True)
    return wrapper

print("Before reduce assignment", flush=True)
reduce = _warn_python_reduce_kwargs(reduce)
print("After reduce assignment", flush=True)
