# fractions.py - Minimal stub for fractions module.
# Fraction is a stub class; full rational arithmetic not implemented. See STUBS.md.

class Fraction:
    """Stub: stores numerator and denominator; minimal __str__. Full rational arithmetic not implemented."""

    def __init__(self, numerator=0, denominator=1):
        if denominator == 0:
            raise ZeroDivisionError("Fraction(..., 0)")
        self._n = numerator
        self._d = denominator

    def __str__(self):
        return "%s/%s" % (self._n, self._d)
