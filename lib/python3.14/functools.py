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
    """Decorator that caches up to maxsize most recent calls. maxsize=0 disables cache; maxsize=None unbounded."""
    def decorator(func):
        cache = {}
        cache_order = []

        def wrapper(*args, **kwargs):
            if maxsize == 0:
                return func(*args, **kwargs)
            key = args
            if kwargs:
                key = key + (tuple(sorted(kwargs.items())),)
            if typed:
                key = tuple((k, type(k)) for k in key)
            if key in cache:
                val = cache[key]
                if maxsize is not None and cache_order:
                    cache_order.remove(key)
                    cache_order.append(key)
                return val
            result = func(*args, **kwargs)
            if maxsize is not None and len(cache) >= maxsize and cache_order:
                oldest = cache_order.pop(0)
                del cache[oldest]
            cache[key] = result
            if maxsize is not None:
                cache_order.append(key)
            return result
        return wrapper
    return decorator
