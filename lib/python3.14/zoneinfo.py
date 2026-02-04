"""
Minimal zoneinfo stub for protoPython.
ZoneInfo returns a stub object; full implementation requires IANA timezone database.
"""

class ZoneInfo:
    """Stub: represents a timezone. Full impl requires tzdata."""

    def __init__(self, key):
        self._key = key

    def __str__(self):
        return self._key

    def __repr__(self):
        return "ZoneInfo(%r)" % self._key
