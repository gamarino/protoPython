"""
Minimal logging for protoPython.
getLogger, basicConfig, Handler, Formatter, StreamHandler.
"""

_loggers = {}

def getLogger(name=None):
    """Return a logger. If basicConfig was used, root may have handlers."""
    n = name if name is not None else "root"
    if n not in _loggers:
        _loggers[n] = _Logger(n)
    return _loggers[n]

def basicConfig(**kwargs):
    """Configure root logger with StreamHandler to stderr if no handlers yet."""
    root = getLogger("root")
    if not root.handlers:
        import sys
        h = StreamHandler(sys.stderr if hasattr(sys, 'stderr') else None)
        root.addHandler(h)
        if 'level' in kwargs:
            root.setLevel(kwargs['level'])
        if 'format' in kwargs:
            h.setFormatter(Formatter(kwargs['format']))

class Formatter:
    """Minimal formatter: format string with %(message)s."""
    def __init__(self, fmt=None):
        self.fmt = fmt or "%(message)s"

    def format(self, record):
        try:
            return self.fmt % record
        except (TypeError, KeyError):
            return str(record.get('msg', record) if isinstance(record, dict) else getattr(record, 'msg', record))

class Handler:
    """Base handler; subclasses override emit."""
    def __init__(self):
        self.formatter = None

    def setFormatter(self, fmt):
        self.formatter = fmt

    def emit(self, record):
        pass

    def handle(self, record):
        self.emit(record)

class StreamHandler(Handler):
    """Write log records to a stream."""
    def __init__(self, stream=None):
        super().__init__()
        self.stream = stream

    def emit(self, record):
        if self.stream:
            msg = self.formatter.format(record) if self.formatter else (record.get('msg', '') if isinstance(record, dict) else getattr(record, 'msg', ''))
            try:
                self.stream.write(msg + '\n')
            except (AttributeError, OSError):
                pass

class _Logger:
    def __init__(self, name):
        self.name = name
        self.handlers = []
        self.level = 0

    def setLevel(self, level):
        self.level = level

    def addHandler(self, h):
        self.handlers.append(h)

    def _log(self, level, msg, *args, **kwargs):
        message = msg % args if args else msg
        record = {'msg': msg, 'message': message, 'levelname': level}
        for h in self.handlers:
            h.handle(record)

    def info(self, msg, *args, **kwargs):
        self._log("INFO", msg, *args, **kwargs)

    def warning(self, msg, *args, **kwargs):
        self._log("WARNING", msg, *args, **kwargs)

    def error(self, msg, *args, **kwargs):
        self._log("ERROR", msg, *args, **kwargs)

    def debug(self, msg, *args, **kwargs):
        self._log("DEBUG", msg, *args, **kwargs)

class _StubLogger:
    """Backward compat: no-op logger."""
    def info(self, msg, *args, **kwargs):
        pass
    def warning(self, msg, *args, **kwargs):
        pass
    def error(self, msg, *args, **kwargs):
        pass
    def debug(self, msg, *args, **kwargs):
        pass
