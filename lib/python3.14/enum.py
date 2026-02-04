# enum.py - Minimal Enum base: name, value, __repr__.

class Enum:
    """Minimal Enum base. Instances have .name and .value. Use Enum(value) or Enum(name, value)."""

    def __init__(self, value, name=None):
        self._value = value
        self._name = name if name is not None else str(value)

    @property
    def name(self):
        return self._name

    @property
    def value(self):
        return self._value

    def __repr__(self):
        return '<%s.%s: %s>' % (type(self).__name__, self._name, self._value)


def auto():
    """Placeholder for auto-assigned enum value. Caller should replace with actual value."""
    return object()
