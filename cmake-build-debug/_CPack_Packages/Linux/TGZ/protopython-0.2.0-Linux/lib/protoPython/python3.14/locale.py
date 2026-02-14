"""
Minimal locale stub for protoPython.
getlocale, setlocale, getpreferredencoding; full implementation requires platform locale.
"""

def getlocale(category=None):
    """Stub: return (None, None) or ('C', 'UTF-8')."""
    return (None, None)

def setlocale(category, locale=None):
    """Stub: return 'C' or no-op."""
    return 'C'

def getpreferredencoding(do_setlocale=True):
    """Stub: return 'UTF-8'."""
    return 'UTF-8'

def getdefaultlocale():
    """Stub: return (None, None)."""
    return (None, None)
