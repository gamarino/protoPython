# collections.py - Minimal placeholder
# Counter stub for compatibility
class Counter(dict):
    """Minimal Counter stub. Behaves like dict."""
    pass

class defaultdict(dict):
    """Minimal defaultdict stub."""
    def __init__(self, default_factory=None, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.default_factory = default_factory
    def __missing__(self, key):
        if self.default_factory is None:
            raise KeyError(key)
        self[key] = self.default_factory()
        return self[key]
