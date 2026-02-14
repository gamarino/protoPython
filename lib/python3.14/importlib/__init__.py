def import_module(name, package=None):
     return __import__(name)

def reload(module): return module

class machinery:
    class ModuleSpec: pass
    class BuiltinImporter: pass
    class FrozenImporter: pass

class util:
    def find_spec(name, package=None): return None
    def module_from_spec(spec): return None
