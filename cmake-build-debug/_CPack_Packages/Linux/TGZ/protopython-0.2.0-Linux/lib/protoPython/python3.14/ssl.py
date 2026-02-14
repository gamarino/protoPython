"""
ssl stub for protoPython.
Full implementation requires OpenSSL/libssl and TLS support.
wrap_socket raises NotImplementedError when invoked.
"""

class SSLContext:
    """Stub: wrap_socket raises NotImplementedError. Full impl requires TLS."""

    def wrap_socket(self, sock, server_side=False, do_handshake_on_connect=True, **kwargs):
        raise NotImplementedError("ssl.wrap_socket requires native TLS; not implemented in protoPython")


def wrap_socket(sock, keyfile=None, certfile=None, server_side=False, **kwargs):
    """Stub: raises NotImplementedError. Full impl requires TLS."""
    raise NotImplementedError("ssl.wrap_socket requires native TLS; not implemented in protoPython")
