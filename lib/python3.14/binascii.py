"""
Minimal binascii stub for protoPython.
hexlify, unhexlify, b2a_base64, a2b_base64 are stubs; full implementation requires base encodings.
"""

def hexlify(data, sep=b'', bytes_per_sep=1):
    """Stub: return empty bytes. Full impl requires hex encoding."""
    return b''

def unhexlify(data):
    """Stub: return empty bytes. Full impl requires hex decoding."""
    return b''

def b2a_base64(data, *, newline=True):
    """Stub: return empty bytes. Full impl requires base64 encoding."""
    return b''

def a2b_base64(data):
    """Stub: return empty bytes. Full impl requires base64 decoding."""
    return b''
