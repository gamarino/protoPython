# secrets: random tokens using random module.
# Uses random.getrandbits; for production use a proper CSPRNG.

import random

_ALPHABET = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_'

def token_hex(nbytes=32):
    """Return a random hex string of 2*nbytes characters."""
    return hex(random.getrandbits(nbytes * 8))[2:].zfill(nbytes * 2)

def token_urlsafe(nbytes=32):
    """Return a random URL-safe base64-style string."""
    nbits = nbytes * 8
    n = random.getrandbits(nbits)
    out = []
    for _ in range((nbits + 5) // 6):
        out.append(_ALPHABET[n & 63])
        n = n >> 6
    return ''.join(out)
