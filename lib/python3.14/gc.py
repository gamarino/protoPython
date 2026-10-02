"""The gc module for protoPython.

protoCore's collector runs concurrently and is not driven from Python: these
functions keep CPython's API so that library code using it runs. enable() and
disable() record the state isenabled() reports; they do not pause protoCore's
collector.
"""

_enabled = True


def isenabled():
    """Return True if automatic collection is enabled (see the module note)."""
    return _enabled


def enable():
    global _enabled
    _enabled = True


def disable():
    global _enabled
    _enabled = False

def collect(generation=2):
    return 0

def set_debug(flags):
    pass

def get_debug():
    return 0

def get_objects(generation=None):
    return []

def get_stats():
    return []

def get_count():
    return (0, 0, 0)

def get_threshold():
    return (700, 10, 10)

def set_threshold(*args):
    pass

def get_referrers(*objs):
    return []

def get_referents(*objs):
    return []

def is_tracked(obj):
    return False

def is_finalized(obj):
    return False

def freeze():
    pass

def unfreeze():
    pass

def get_freeze_count():
    return 0

garbage = []
callbacks = []
DEBUG_STATS = 1
DEBUG_COLLECTABLE = 2
DEBUG_UNCOLLECTABLE = 4
DEBUG_SAVEALL = 32
DEBUG_LEAK = DEBUG_STATS | DEBUG_COLLECTABLE | DEBUG_UNCOLLECTABLE | DEBUG_SAVEALL
