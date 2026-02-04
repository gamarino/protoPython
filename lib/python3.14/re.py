# re.py - Minimal stub for regex compatibility.
# Native ReModule may be used when available; this lib stub returns None/[]/string. See STUBS.md.

def compile(pattern, flags=0):
    """Stub: returns None."""
    return None

def match(pattern, string, flags=0):
    """Stub: returns None."""
    return None

def search(pattern, string, flags=0):
    """Stub: returns None."""
    return None

def findall(pattern, string, flags=0):
    """Stub: returns empty list."""
    return []

def sub(pattern, repl, string, count=0, flags=0):
    """Stub: returns string unchanged."""
    return string
