"""
Minimal logging stub for protoPython.
getLogger, basicConfig, info, warning, error, debug; full implementation requires log levels and handlers.
"""

def getLogger(name=None):
    """Stub: return a no-op logger object."""
    return _StubLogger()

def basicConfig(**kwargs):
    """Stub: no-op. Full impl requires handler configuration."""
    pass

class _StubLogger:
    def info(self, msg, *args, **kwargs):
        pass
    def warning(self, msg, *args, **kwargs):
        pass
    def error(self, msg, *args, **kwargs):
        pass
    def debug(self, msg, *args, **kwargs):
        pass
