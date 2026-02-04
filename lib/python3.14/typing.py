# typing.py - Minimal stub for type hints (Any, List, Dict, Optional, Union).
# Full typing module behavior (generics, etc.) is not implemented.

Any = object()  # Placeholder for typing.Any in annotations.

def _alias(name, base):
    """Return a simple alias for use in type hints."""
    return base

List = list
Dict = dict
Optional = object()  # Placeholder for typing.Optional (Optional[X] ~ Union[X, None]).
Union = object()  # Placeholder for typing.Union.
