"""
Minimal ssl stub for protoPython.
SSLContext, wrap_socket; full implementation requires OpenSSL/libssl.
"""

class SSLContext:
    """Stub: no-op. wrap_socket returns None. Full impl requires TLS."""

    def wrap_socket(self, sock, server_side=False, do_handshake_on_connect=True, **kwargs):
        return None

def wrap_socket(sock, keyfile=None, certfile=None, server_side=False, **kwargs):
    """Stub: return None. Full impl requires TLS."""
    return None
