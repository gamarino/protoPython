# Lists the runtime builds natively are ordinary mutable lists.  The shared
# builder made an immutable object, so append(), extend(), item assignment
# and sort() on the result of os.listdir() (and on a package's __path__)
# silently changed nothing.
import json
import os
import re
import struct

for name, l in [("listdir", os.listdir(os.path.dirname(os.__file__))),
                ("__path__", json.__path__),
                ("re.split", re.split(",", "a,b")),
                ("re.findall", re.findall("a", "aba")),
                ("struct", list(struct.unpack("<2h", b"\x01\x00\x02\x00")))]:
    n = len(l)
    l.append("z")
    l.extend(["y"])
    l[0] = "first"
    assert len(l) == n + 2 and l[0] == "first" and l[-2:] == ["z", "y"], (name, l[:3], len(l), n)
    del l[-1]
    assert len(l) == n + 1, name
print("native_lists_mutable: ok")
