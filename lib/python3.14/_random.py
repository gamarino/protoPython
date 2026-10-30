"""Mersenne Twister core for the `random` module.

`random.Random` subclasses the class defined here and relies on it for the raw
bit source: `random()`, `getrandbits()`, `seed()`, `getstate()` and
`setstate()`.

The generator is MT19937 with CPython's seeding (`init_by_array` over the
32-bit words of `abs(seed)`), so an integer seed produces the same stream as
CPython: `random.Random(42).random()` is 0.6394267984578837 in both.  The
generator itself runs natively (module `_mt19937`); a pure-Python generator
cost about 25 microseconds per draw.
"""

import os
import _mt19937

N = 624

_new_state = _mt19937.new_state
_init_by_array = _mt19937.init_by_array
_genrand_uint32 = _mt19937.genrand_uint32
_random = _mt19937.random
_getrandbits32 = _mt19937.getrandbits32


class Random:
    def __init__(self, x=None):
        self.seed(x)

    def seed(self, a=None):
        """Seed the generator.

        None seeds from the operating system's randomness source, so an
        unseeded generator is still unpredictable.  An int seeds exactly as
        CPython does.  `random.Random.seed` has already converted str, bytes
        and bytearray seeds to an int before calling this.
        """
        if a is None:
            a = int.from_bytes(os.urandom(32), "big")
        elif isinstance(a, float):
            a = int.from_bytes(repr(a).encode(), "big")
        elif not isinstance(a, int):
            raise TypeError("The only supported seed types are: None, int, "
                            "float, str, bytes, and bytearray.")

        if a < 0:
            a = -a

        key = []
        if a == 0:
            key.append(0)
        while a != 0:
            key.append(a % 4294967296)
            a = a // 4294967296
        state = _new_state()
        _init_by_array(state, key)
        self._mt_state = state

    def getstate(self):
        return _mt19937.getstate(self._mt_state)

    def setstate(self, state):
        if len(state) != N + 1:
            raise ValueError("state vector is the wrong size")
        words = tuple(int(state[i]) & 0xFFFFFFFF for i in range(N))
        mti = int(state[N])
        if mti < 0 or mti > N:
            raise ValueError("state vector invalid read position")
        if not hasattr(self, "_mt_state"):
            self._mt_state = _new_state()
        _mt19937.setstate(self._mt_state, words, mti)

    def random(self):
        """A float in [0.0, 1.0), from 53 random bits — CPython's recipe."""
        return _random(self._mt_state)

    def getrandbits(self, k):
        """An int with k random bits."""
        if not isinstance(k, int):
            raise TypeError("number of bits must be an integer")
        if k < 0:
            raise ValueError("number of bits must be non-negative")
        if k == 0:
            return 0
        state = self._mt_state
        if k <= 32:
            return _getrandbits32(state, k)

        result = 0
        shift = 0
        remaining = k
        while remaining > 0:
            r = _genrand_uint32(state)
            if remaining < 32:
                r >>= (32 - remaining)
            result |= r << shift
            shift += 32
            remaining -= 32
        return result
