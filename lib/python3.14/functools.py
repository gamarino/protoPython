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
    """Placeholder: returns a no-op decorator that does not update the wrapper."""
    def decorator(func):
        return func
    return decorator

def lru_cache(maxsize=128, typed=False):
    """Stub: returns a decorator that returns the callable unchanged (no caching)."""
    def decorator(func):
        return func
    return decorator
