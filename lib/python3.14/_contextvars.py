class ContextVar:
    def __init__(self, name, *, default=None):
        self.name = name
        self.default = default
    def get(self, default=None):
        if hasattr(self, 'value'):
            return self.value
        if self.default is not None:
            return self.default
        return default
    def set(self, value):
        self.value = value

class Context:
    def run(self, callable, *args, **kwargs):
        return callable(*args, **kwargs)

def copy_context():
    return Context()
