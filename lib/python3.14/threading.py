import _thread

def Lock():
    return _thread.allocate_lock()

def RLock():
    return _thread.allocate_rlock()

def active_count():
    return _thread._count()

class Thread:
    def __init__(self, target=None, args=(), name=None):
        self.target = target
        self.args = args
        self.ident = None
        self.name = name or "Thread"

    def start(self):
        self.ident = _thread.start_new_thread(self.target, self.args)

    def join(self):
        if self.ident is not None:
            handle = _thread._get_thread_handle(self.ident)
            if handle:
                _thread.join_thread(handle)

    def is_alive(self):
        if self.ident is None: return False
        handle = _thread._get_thread_handle(self.ident)
        if not handle: return False
        return _thread.is_alive(handle)
