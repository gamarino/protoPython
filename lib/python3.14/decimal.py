# decimal.py - Minimal stub for decimal module.
# Decimal is a stub class storing a value; full arithmetic not implemented. See STUBS.md.

class Decimal:
    """Stub: stores value; minimal __str__. Full decimal arithmetic not implemented."""

    def __init__(self, value="0", context=None):
        self._value = value if isinstance(value, str) else str(value)

    def __str__(self):
        return self._value
