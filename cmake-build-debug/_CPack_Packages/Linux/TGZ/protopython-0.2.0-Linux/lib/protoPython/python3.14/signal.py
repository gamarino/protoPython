# signal.py - Python wrapper for _signal

try:
    import _signal
    _has_signal = True
except ImportError:
    _has_signal = False

if _has_signal:
    SIGINT = _signal.SIGINT
    SIGTERM = _signal.SIGTERM
    SIG_DFL = _signal.SIG_DFL

    def signal(sig, handler):
        return _signal.signal(sig, handler)

    def getsignal(sig):
        return _signal.getsignal(sig)
else:
    # Minimal stubs if not available
    SIGINT = 2
    SIGTERM = 15
    SIG_DFL = None
    
    def signal(sig, handler):
        return None
        
    def getsignal(sig):
        return None
