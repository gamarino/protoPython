# functools.py - Minimal stub; wraps placeholder.

class partial:
    """Stub: minimal partial application placeholder."""
    def __init__(self, func, *args, **kwargs):
        self.func = func
        self.args = args
        self.keywords = kwargs
    def __call__(self, *args, **kwargs):
        return self.func(*(self.args + args), **{**self.keywords, **kwargs})

def reduce(function, iterable, initializer=None):
    """Stub: returns initializer or first element."""
    it = iter(iterable)
    if initializer is None:
        try:
            initializer = next(it)
        except StopIteration:
            raise TypeError("reduce() of empty sequence with no initial value")
    for element in it:
        initializer = function(initializer, element)
    return initializer

def wraps(wrapped, assigned=None, updated=None):
    """Decorator that copies __name__, __doc__, __module__ from wrapped to the wrapper."""
    if assigned is None:
        assigned = ('__module__', '__name__', '__qualname__', '__doc__', '__annotations__')
    if updated is None:
        updated = ('__dict__',)

    def decorator(func):
        for attr in assigned:
            if hasattr(wrapped, attr):
                try:
                    setattr(func, attr, getattr(wrapped, attr))
                except (AttributeError, TypeError):
                    pass
        for attr in updated:
            if hasattr(wrapped, attr):
                try:
                    getattr(func, attr).update(getattr(wrapped, attr, {}))
                except (AttributeError, TypeError):
                    pass
        return func
    return decorator

def lru_cache(maxsize=128, typed=False):
    """Stub: returns a decorator that returns the callable unchanged (no caching)."""
    def decorator(func):
        return func
    return decorator
