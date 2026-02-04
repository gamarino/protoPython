# argparse.py - Minimal stub for argument parsing. Full behavior not implemented.


class Namespace:
    """Minimal namespace object for parse_args result."""
    def __init__(self, **kwargs):
        for k, v in kwargs.items():
            setattr(self, k, v)


class ArgumentParser:
    """Minimal stub: add_argument and parse_args return defaults."""

    def __init__(self, **kwargs):
        self._args = {}

    def add_argument(self, *args, **kwargs):
        """Stub: record optional dest; no parsing."""
        dest = kwargs.get('dest')
        if dest is None and args:
            dest = args[0].lstrip('-').replace('-', '_')
        if dest:
            self._args[dest] = kwargs.get('default', None)
        return None

    def parse_args(self, args=None):
        """Stub: return Namespace with default/recorded attributes."""
        return Namespace(**self._args)
