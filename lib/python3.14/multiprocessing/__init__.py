"""
Minimal multiprocessing stub for protoPython.
Process, Pool; full implementation requires process creation and IPC.
"""

class Process:
    """Stub: no-op. start/join do nothing. Full impl requires fork/spawn."""

    def __init__(self, target=None, args=(), kwargs=None):
        self.target = target
        self.args = args or ()
        self.kwargs = kwargs or {}

    def start(self):
        pass

    def join(self, timeout=None):
        pass

    def is_alive(self):
        return False

def Pool(processes=None, initializer=None, initargs=(), **kwargs):
    """Stub: return placeholder. Full impl requires worker pool."""
    return None
