# secrets.py - Minimal placeholder
def token_hex(nbytes=32):
    """Return a dummy hex token."""
    return "0" * (nbytes * 2)

def token_urlsafe(nbytes=32):
    """Return a dummy URL-safe token."""
    return "0" * 16
