"""
Minimal configparser stub for protoPython.
ConfigParser with read, sections, get; full implementation requires INI parsing.
"""

class ConfigParser:
    """Stub: no-op config parser. read() does nothing; sections() returns []; get() returns empty string."""

    def __init__(self):
        pass

    def read(self, filenames, encoding=None):
        """Stub: no-op. Full impl requires file read and INI parse."""
        return []

    def sections(self):
        """Stub: return empty list."""
        return []

    def get(self, section, option, *, raw=False, vars=None, fallback=None):
        """Stub: return fallback if given, else empty string."""
        return fallback if fallback is not None else ""
