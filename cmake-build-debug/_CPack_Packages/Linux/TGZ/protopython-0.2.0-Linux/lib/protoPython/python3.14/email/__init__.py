# email package - Minimal stub for protoPython stdlib.
# message_from_string parses headers only.

__all__ = ['message_from_string']

class _Message:
    """Minimal message with headers only."""
    def __init__(self, headers=None):
        self._headers = headers if headers is not None else {}

    def get(self, key, failobj=None):
        return self._headers.get(key, failobj)

    def __getitem__(self, key):
        return self._headers[key]

def message_from_string(s):
    """Parse headers only; body is ignored. Returns _Message with get(key)."""
    if not s or not isinstance(s, str):
        return _Message({})
    headers = {}
    for line in s.splitlines():
        line = line.strip()
        if not line:
            break
        if ':' in line:
            k, _, v = line.partition(':')
            headers[k.strip()] = v.strip()
    return _Message(headers)
