"""
Minimal shelve stub for protoPython.
open returns a dict-like stub; full implementation requires dbm and serialization.
"""

def open(filename, flag='c', protocol=None, writeback=False):
    """Stub: return minimal dict-like object. get/keys return empty; __setitem__ no-op."""
    return _ShelveStub()

class _ShelveStub:
    """Minimal dict-like stub for shelve.open."""

    def get(self, key, default=None):
        return default

    def keys(self):
        return []

    def __getitem__(self, key):
        raise KeyError(key)

    def __setitem__(self, key, value):
        pass

    def __contains__(self, key):
        return False

    def close(self):
        pass
