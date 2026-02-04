"""
Minimal getpass stub for protoPython.
getpass and getuser; full implementation requires tty/password input.
"""

def getpass(prompt="Password: ", stream=None):
    """Stub: return placeholder. Full impl requires tty input."""
    return ""

def getuser():
    """Return username from os.environ (USER or LOGNAME)."""
    try:
        import os
        return os.environ.get('USER') or os.environ.get('LOGNAME') or ''
    except Exception:
        return ""
