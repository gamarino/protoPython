import os

class Random:
    def __init__(self, x=None):
        pass

    def seed(self, a=None):
        pass

    def getstate(self):
        return (1, (0,)*624, None)

    def setstate(self, state):
        pass

    def random(self):
        b = os.urandom(7)
        return int.from_bytes(b, 'little') / (1 << 56)

    def getrandbits(self, k):
        if k <= 0:
            raise ValueError("number of bits must be greater than zero")
        num_bytes = (k + 7) // 8
        b = os.urandom(num_bytes)
        val = int.from_bytes(b, 'little')
        val &= (1 << k) - 1
        return val
