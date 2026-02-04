# functools.py - Minimal stub; wraps placeholder.

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
