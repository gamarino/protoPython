"""
Minimal getpass stub for protoPython.
getpass and getuser; full implementation requires tty/password input.
"""

def getpass(prompt="Password: ", stream=None):
    """Stub: return placeholder. Full impl requires tty input."""
    return ""

def getuser():
    """Stub: return placeholder. Full impl requires os.environ or pwd."""
    return ""
