try:
    from _types import *
except ImportError:
    MappingProxyType = type(type.__dict__)
    print("MappingProxyType is:", MappingProxyType)
