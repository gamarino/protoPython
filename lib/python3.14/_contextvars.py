import threading

__all__ = ('Context', 'ContextVar', 'Token', 'copy_context')

class _Sentinel:
    pass

MISSING = _Sentinel()

_state = threading.local()

def _get_current_context():
    try:
        return _state.context
    except AttributeError:
        ctx = Context()
        _state.context = ctx
        return ctx

def _set_current_context(ctx):
    if ctx is None:
        try:
            del _state.context
        except AttributeError:
            pass
    else:
        _state.context = ctx


class Token:
    MISSING = MISSING
    
    def __init__(self, context, var, old_value):
        self._context = context
        self._var = var
        self._old_value = old_value
        self._used = False
        
    @property
    def var(self):
        return self._var
        
    @property
    def old_value(self):
        return self._old_value
        
    def __repr__(self):
        old = "<Token.MISSING>" if self._old_value is MISSING else repr(self._old_value)
        return f"<Token var={self._var!r} at {id(self):#x}>"


class ContextVar:
    def __init__(self, name, *, default=MISSING):
        if not isinstance(name, str):
            raise TypeError("context variable name must be a string")
        self._name = name
        self._default = default
        
    @property
    def name(self):
        return self._name
        
    def get(self, default=MISSING):
        ctx = _get_current_context()
        if self in ctx._data:
            return ctx._data[self]
        if default is not MISSING:
            return default
        if self._default is not MISSING:
            return self._default
        raise LookupError(f"Context variable {self._name!r} has no value")
        
    def set(self, value):
        ctx = _get_current_context()
        old_value = getattr(ctx, 'get', lambda k, d: ctx._data.get(k, d))(self, MISSING)
        ctx._data[self] = value
        return Token(ctx, self, old_value)
        
    def reset(self, token):
        if not isinstance(token, Token):
            raise TypeError("token argument must be a Token")
        if token._var is not self:
            raise ValueError("Token was created by a different ContextVar")
        if token._used:
            raise RuntimeError("Token has already been used once")
            
        ctx = _get_current_context()
        if token._context is not ctx:
            raise ValueError("Token was created in a different Context")
            
        token._used = True
        if token.old_value is MISSING:
            ctx._data.pop(self, None)
        else:
            ctx._data[self] = token.old_value
            
    def __hash__(self):
        return id(self)
        
    def __eq__(self, other):
        return self is other
            
    def __repr__(self):
        return f"<ContextVar name={self._name!r} at {id(self):#x}>"


class Context:
    def __init__(self):
        self._data = {}
        
    def run(self, callable, *args, **kwargs):
        try:
            prev_context = _state.context
        except AttributeError:
            prev_context = None
            
        if prev_context is self:
            raise RuntimeError("cannot enter context: it is already the current context")
            
        _set_current_context(self)
        
        has_err = False
        err = None
        res = None
        try:
            res = callable(*args, **kwargs)
        except BaseException as e:
            has_err = True
            err = e
            
        _set_current_context(prev_context)
        
        if has_err:
            raise err
        return res
                
    def copy(self):
        new_ctx = Context()
        new_ctx._data = dict(self._data)
        return new_ctx
        
    def __getitem__(self, key):
        if not isinstance(key, ContextVar):
            raise TypeError("key must be a ContextVar")
        return self._data[key]
        
    def get(self, key, default=None):
        if not isinstance(key, ContextVar):
            raise TypeError("key must be a ContextVar")
        return self._data.get(key, default)
        
    def keys(self):
        return self._data.keys()
        
    def values(self):
        return self._data.values()
        
    def items(self):
        return self._data.items()
        
    def __contains__(self, key):
        if not isinstance(key, ContextVar):
            raise TypeError("key must be a ContextVar")
        return key in self._data
        
    def __len__(self):
        return len(self._data)
        
    def __iter__(self):
        return iter(self._data)
        
    def __repr__(self):
        return f"<Context object at {id(self):#x}>"

def copy_context():
    return _get_current_context().copy()
