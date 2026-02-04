"""
Minimal pprint stub for protoPython.
pprint, pformat, PrettyPrinter; full implementation requires recursive pretty-printing and stream handling.
"""

def pprint(object, stream=None, indent=1, width=80, depth=None, *, compact=False):
    """Stub: no-op. Full impl requires stream write and recursive formatting."""
    pass

def pformat(object, indent=1, width=80, depth=None, *, compact=False):
    """Stub: return str(object). Full impl requires pretty-printed string."""
    return str(object)

class PrettyPrinter:
    """Stub class for pretty-printing. Full impl requires stream and format logic."""
    def __init__(self, indent=1, width=80, depth=None, stream=None, *, compact=False):
        pass
    def pprint(self, object):
        """Stub: no-op."""
        pass
    def pformat(self, object):
        """Stub: return str(object)."""
        return str(object)
