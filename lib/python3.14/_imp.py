def acquire_lock(): pass
def release_lock(): pass
def lock_held(): return False
def is_builtin(name): return False
def is_frozen(name): return False
def get_frozen_object(name): raise ImportError(name)
def init_frozen(name): raise ImportError(name)
def get_tag(): return "protopy-314"
