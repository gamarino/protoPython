# fractions.py - Minimal Fraction with __add__.

def _gcd(a, b):
    a, b = abs(a), abs(b)
    while b:
        a, b = b, a % b
    return a

def _lcm(a, b):
    return a * b // _gcd(a, b) if a and b else 0

class Fraction:
    """Stores n, d; __str__, __repr__; __add__ using LCM."""

    def __init__(self, numerator=0, denominator=1):
        if denominator == 0:
            raise ZeroDivisionError("Fraction(..., 0)")
        self._n = int(numerator)
        self._d = int(denominator)

    def __str__(self):
        return "%s/%s" % (self._n, self._d)

    def __repr__(self):
        return "Fraction(%s, %s)" % (self._n, self._d)

    def __add__(self, other):
        if isinstance(other, Fraction):
            den = _lcm(self._d, other._d)
            num = self._n * (den // self._d) + other._n * (den // other._d)
            g = _gcd(num, den)
            return Fraction(num // g, den // g)
        return self + Fraction(other, 1)

    def __mul__(self, other):
        if isinstance(other, Fraction):
            num = self._n * other._n
            den = self._d * other._d
            g = _gcd(num, den)
            return Fraction(num // g, den // g)
        return self * Fraction(other, 1)
