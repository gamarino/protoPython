# decimal.py - Minimal Decimal with basic arithmetic.

class Decimal:
    """Stores value as string; __str__, __repr__; basic __add__/__sub__."""

    def __init__(self, value="0", context=None):
        self._value = value if isinstance(value, str) else str(value)

    def __str__(self):
        return self._value

    def __repr__(self):
        return "Decimal('%s')" % self._value

    def __add__(self, other):
        if isinstance(other, Decimal):
            return Decimal(str(float(self._value) + float(other._value)))
        return Decimal(str(float(self._value) + float(other)))

    def __sub__(self, other):
        if isinstance(other, Decimal):
            return Decimal(str(float(self._value) - float(other._value)))
        return Decimal(str(float(self._value) - float(other)))
