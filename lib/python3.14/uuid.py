# uuid: UUID generation using random module.

import random

def uuid4():
    """Return a random UUID4 string (e.g. xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx)."""
    n = random.getrandbits(128)
    n = (n & 0xffffffffffff0fff3fffffffffffffff) | 0x00000000000040008000000000000000
    h = hex(n)[2:].zfill(32)
    return h[0:8] + '-' + h[8:12] + '-' + h[12:16] + '-' + h[16:20] + '-' + h[20:32]
