# random: simple PRNG (LCG) for deterministic random numbers.
# Seed with seed() for reproducibility; default seed from a fixed value.

_state = 12345

def seed(a=None):
    global _state
    if a is None:
        _state = 12345
    else:
        _state = int(a) & 0x7fffffff
        if _state == 0:
            _state = 1

def _next():
    global _state
    _state = (1103515245 * _state + 12345) & 0x7fffffff
    return _state / 0x80000000

def random():
    """Return next random float in [0.0, 1.0)."""
    return _next()

def randint(a, b):
    """Return random integer N such that a <= N <= b."""
    return a + int(_next() * (b - a + 1))

def choice(seq):
    """Choose a random element from non-empty sequence."""
    n = len(seq)
    if n == 0:
        raise IndexError("Cannot choose from an empty sequence")
    return seq[int(_next() * n)]

def getrandbits(k):
    """Return a non-negative integer with k random bits."""
    n = 0
    for _ in range((k + 31) // 32):
        n = (n << 32) | (int(_next() * 0x100000000) & 0xffffffff)
    return n >> (32 - k % 32) if k % 32 else n
