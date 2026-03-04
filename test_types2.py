try:
    from _types import *
except ImportError:
    print("inside except!")
    import sys
    def _f(): pass
    FunctionType = type(_f)
    MappingProxyType = type(type.__dict__)
    print("MappingProxyType exists")
print("globals:", list(globals().keys()))
