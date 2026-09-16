"""Mersenne Twister core for the `random` module.

`random.Random` subclasses the class defined here and relies on it for the raw
bit source: `random()`, `getrandbits()`, `seed()`, `getstate()` and
`setstate()`.  This used to be a stub that ignored the seed and read
`os.urandom()` on every call, which made `random.Random(seed)` non-reproducible
— the property the seed exists for.

The generator is MT19937 with CPython's seeding (`init_by_array` over the
32-bit words of `abs(seed)`), so an integer seed produces the same stream as
CPython: `random.Random(42).random()` is 0.6394267984578837 in both.
"""

import os

N = 624
M = 397
MATRIX_A = 0x9908B0DF
UPPER_MASK = 0x80000000
LOWER_MASK = 0x7FFFFFFF


class Random:
    def __init__(self, x=None):
        self.seed(x)

    # -- seeding -----------------------------------------------------------

    def _init_genrand(self, s):
        mt = self.mt
        mt[0] = s & 0xFFFFFFFF
        for i in range(1, N):
            prev = mt[i - 1]
            mt[i] = (1812433253 * (prev ^ (prev >> 30)) + i) & 0xFFFFFFFF
        self.mti = N

    def _init_by_array(self, init_key):
        self._init_genrand(19650218)
        mt = self.mt
        key_length = len(init_key)
        i = 1
        j = 0
        k = N if N > key_length else key_length
        while k:
            prev = mt[i - 1]
            mt[i] = ((mt[i] ^ ((prev ^ (prev >> 30)) * 1664525))
                     + init_key[j] + j) & 0xFFFFFFFF
            i += 1
            j += 1
            if i >= N:
                mt[0] = mt[N - 1]
                i = 1
            if j >= key_length:
                j = 0
            k -= 1
        k = N - 1
        while k:
            prev = mt[i - 1]
            mt[i] = ((mt[i] ^ ((prev ^ (prev >> 30)) * 1566083941))
                     - i) & 0xFFFFFFFF
            i += 1
            if i >= N:
                mt[0] = mt[N - 1]
                i = 1
            k -= 1
        mt[0] = 0x80000000

    def seed(self, a=None):
        """Seed the generator.

        None seeds from the operating system's randomness source, so an
        unseeded generator is still unpredictable.  An int seeds exactly as
        CPython does.  `random.Random.seed` has already converted str, bytes
        and bytearray seeds to an int before calling this.
        """
        # The state is allocated here rather than in __init__, because
        # random.Random subclasses this class and its __init__ does not call
        # ours — CPython builds the Mersenne state in tp_new, not in __init__.
        # Every path that produces numbers goes through seed() first.
        self.mt = [0] * N
        self.mti = N + 1

        if a is None:
            a = int.from_bytes(os.urandom(32), "big")
        elif isinstance(a, float):
            # Deterministic for a given float, which is what reproducibility
            # asks for; the exact mapping is not CPython's.
            a = int.from_bytes(repr(a).encode(), "big")
        elif not isinstance(a, int):
            raise TypeError("The only supported seed types are: None, int, "
                            "float, str, bytes, and bytearray.")

        if a < 0:
            a = -a

        key = []
        if a == 0:
            key.append(0)
        # The seed is split into 32-bit words with arithmetic, not with bit
        # operations: a seed wider than 64 bits is a LargeInteger, and several
        # operations on one still convert through a 64-bit integer and raise
        # "LargeInteger value exceeds long long range".  `%` and `//` do not,
        # and `a != 0` avoids truth-testing the value.
        while a != 0:
            key.append(a % 4294967296)
            a = a // 4294967296
        self._init_by_array(key)

    # -- state -------------------------------------------------------------

    def getstate(self):
        return tuple(self.mt) + (self.mti,)

    def setstate(self, state):
        if len(state) != N + 1:
            raise ValueError("state vector is the wrong size")
        mt = []
        for i in range(N):
            mt.append(int(state[i]) & 0xFFFFFFFF)
        self.mt = mt
        mti = int(state[N])
        if mti < 0 or mti > N:
            raise ValueError("state vector invalid read position")
        self.mti = mti

    # -- generation --------------------------------------------------------

    def _genrand_uint32(self):
        mt = self.mt
        if self.mti >= N:
            if self.mti == N + 1:
                self._init_genrand(5489)
            for kk in range(N - M):
                y = (mt[kk] & UPPER_MASK) | (mt[kk + 1] & LOWER_MASK)
                mt[kk] = mt[kk + M] ^ (y >> 1) ^ (MATRIX_A if y & 1 else 0)
            for kk in range(N - M, N - 1):
                y = (mt[kk] & UPPER_MASK) | (mt[kk + 1] & LOWER_MASK)
                mt[kk] = mt[kk + (M - N)] ^ (y >> 1) ^ (MATRIX_A if y & 1 else 0)
            y = (mt[N - 1] & UPPER_MASK) | (mt[0] & LOWER_MASK)
            mt[N - 1] = mt[M - 1] ^ (y >> 1) ^ (MATRIX_A if y & 1 else 0)
            self.mti = 0

        y = mt[self.mti]
        self.mti += 1
        y ^= y >> 11
        y ^= (y << 7) & 0x9D2C5680
        y ^= (y << 15) & 0xEFC60000
        y ^= y >> 18
        return y & 0xFFFFFFFF

    def random(self):
        """A float in [0.0, 1.0), from 53 random bits — CPython's recipe."""
        a = self._genrand_uint32() >> 5
        b = self._genrand_uint32() >> 6
        return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0)

    def getrandbits(self, k):
        """An int with k random bits."""
        if not isinstance(k, int):
            raise TypeError("number of bits must be an integer")
        if k < 0:
            raise ValueError("number of bits must be non-negative")
        if k == 0:
            return 0
        if k <= 32:
            return self._genrand_uint32() >> (32 - k)

        result = 0
        shift = 0
        remaining = k
        while remaining > 0:
            r = self._genrand_uint32()
            if remaining < 32:
                r >>= (32 - remaining)
            result |= r << shift
            shift += 32
            remaining -= 32
        return result
