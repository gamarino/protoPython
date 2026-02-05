"""
getpass stub for protoPython.
getuser reads from os.environ. getpass returns ""; full impl requires tty/password input.
"""

def getpass(prompt="Password: ", stream=None):
    """Return empty string (stub). Full impl requires tty/password input."""
    return ""

def getuser():
    """Return username from os.environ (USER or LOGNAME)."""
    try:
        import os
        return os.environ.get('USER') or os.environ.get('LOGNAME') or ''
    except Exception:
        return ""
