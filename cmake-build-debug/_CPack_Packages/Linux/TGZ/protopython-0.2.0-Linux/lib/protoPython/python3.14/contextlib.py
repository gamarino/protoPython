# contextlib.py - Minimal contextmanager decorator.

class _GeneratorContextManager:
    def __init__(self, func, *args, **kwargs):
        self.gen = func(*args, **kwargs)
        self.func, self.args, self.kwargs = func, args, kwargs

    def __enter__(self):
        return next(self.gen)

    def __exit__(self, typ, val, tb):
        if typ is None:
            try:
                next(self.gen)
            except StopIteration:
                return False
        else:
            try:
                self.gen.throw(typ, val, tb)
            except StopIteration:
                return True
        return False

def contextmanager(func):
    """Decorator that turns a generator into a context manager."""
    def inner(*args, **kwargs):
        return _GeneratorContextManager(func, *args, **kwargs)
    return inner
