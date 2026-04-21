import _collections_abc
print(f"MutableMapping: {_collections_abc.MutableMapping} at {hex(id(_collections_abc.MutableMapping))}")
print(f"Mapping: {_collections_abc.Mapping} at {hex(id(_collections_abc.Mapping))}")

import os
print(f"os._Environ bases: {os._Environ.__bases__}")
for base in os._Environ.__bases__:
    print(f"base {base} at {hex(id(base))}")
