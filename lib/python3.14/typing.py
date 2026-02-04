# typing.py - Minimal stub for type hints (Any, List, Dict).
# Full typing module behavior (generics, Optional, etc.) is not implemented.

Any = object()  # Placeholder for typing.Any in annotations.

def _alias(name, base):
    """Return a simple alias for use in type hints."""
    return base

List = list
Dict = dict
