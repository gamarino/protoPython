# abc.py - Minimal placeholder
def abstractmethod(funcobj):
    funcobj.__isabstractmethod__ = True
    return funcobj

class ABC:
    pass
