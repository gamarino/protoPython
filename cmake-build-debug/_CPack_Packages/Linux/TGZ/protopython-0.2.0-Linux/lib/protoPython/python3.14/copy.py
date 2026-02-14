# copy.py - Shallow and deep copy implementation.
# Supports list, dict, tuple, set. Other types returned as-is (copy) or unchanged (deepcopy).
# Deep copy does not handle cycles (no memo); avoid deepcopy of self-referential structures.

def copy(x):
    """Return a shallow copy of x. Supports list, dict, tuple, set; else returns x."""
    try:
        t = type(x).__name__
    except Exception:
        return x
    if t == 'list':
        return list(x)
    if t == 'dict':
        return dict(x)
    if t == 'tuple':
        return tuple(x)
    if t == 'set':
        return set(x)
    return x


def deepcopy(x, memo=None):
    """Return a deep copy of x. Supports list, dict, tuple, set; else returns x. No cycle detection."""
    try:
        t = type(x).__name__
    except Exception:
        return x
    if t == 'list':
        return [deepcopy(e, memo) for e in x]
    if t == 'dict':
        return {k: deepcopy(v, memo) for k, v in x.items()}
    if t == 'tuple':
        return tuple(deepcopy(e, memo) for e in x)
    if t == 'set':
        return set(deepcopy(e, memo) for e in x)
    return x
