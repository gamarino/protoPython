"""
multiprocessing stub for protoPython.
Full implementation requires process creation (fork/spawn) and IPC.
Process.start/join are no-ops; Pool returns None for import compatibility.
"""

class Process:
    """Stub: start/join no-op. Full impl requires fork/spawn and IPC."""

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
    """Return None (stub). Full impl requires worker pool and IPC."""
    return None
