"""
Minimal quopri stub for protoPython.
encode, decode are stubs; full implementation requires quoted-printable encoding.
"""

def encode(input, output, quotetabs=False):
    """Stub: copy input to output. Full impl requires quoted-printable encoding."""
    output.write(input.read())

def decode(input, output, header=False):
    """Stub: copy input to output. Full impl requires quoted-printable decoding."""
    output.write(input.read())
