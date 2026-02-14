class ref:
    def __init__(self, obj, callback=None):
        self.obj = obj
    def __call__(self):
        return self.obj

def getweakrefcount(obj): return 0
def getweakrefs(obj): return []
ProxyType = type(None)
CallableProxyType = type(None)
ReferenceType = ref
