# abc.py - Minimal placeholder. See STUBS.md.
def abstractmethod(funcobj):
    funcobj.__isabstractmethod__ = True
    return funcobj

class ABC:
    pass
