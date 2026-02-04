# difflib.py - Minimal difflib stub for protoPython stdlib.
# Full diff algorithms (SequenceMatcher, unified_diff, etc.) are not implemented.

class SequenceMatcher:
    """Stub: placeholder for SequenceMatcher. get_matching_blocks returns []; ratio returns 0.0."""

    def __init__(self, isjunk=None, a='', b=''):
        self.a = a
        self.b = b

    def get_matching_blocks(self):
        return []

    def ratio(self):
        return 0.0


def unified_diff(a, b, fromfile='', tofile='', fromfiledate='', tofiledate='', n=3, lineterm='\n'):
    """Stub: returns empty list. Full unified diff not implemented."""
    return []
