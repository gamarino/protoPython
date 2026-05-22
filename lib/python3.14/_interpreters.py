# Minimal stub for _interpreters extension
def create(): raise NotImplementedError()
def list_all(): return []
def exists(id): return False
def get_current(): return 0
def get_main(): return 0
class InterpreterError(Exception): pass
