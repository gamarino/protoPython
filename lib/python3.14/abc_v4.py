def abstractmethod(f):
 f.__isabstractmethod__=True
 return f
_abc_invalidation_counter=0
_abc_in_check=set()
def get_cache_token():return _abc_invalidation_counter
class ABCMeta(type):
 def __new__(mcls,name,bases,ns,**kw):
  if "__name__" not in ns:ns["__name__"]=name
  abs=set()
  for k in ns:
   try:
    if getattr(ns[k],"__isabstractmethod__",False):abs.add(k)
   except:pass
  for b in bases:
   for m in getattr(b,"__abstractmethods__",()):
    v=ns.get(m,getattr(b,m,None))
    if getattr(v,"__isabstractmethod__",False):abs.add(m)
  cls=type.__new__(mcls,name,bases,ns)
  cls.__abstractmethods__=frozenset(abs)
  cls._abc_registry=set()
  cls._abc_cache=set()
  cls._abc_negative_cache=set()
  cls._abc_negative_cache_version=_abc_invalidation_counter
  return cls
 def register(cls,sub):
  if not isinstance(sub,type):raise TypeError("classes only")
  if not hasattr(cls,'_abc_registry'):
   cls._abc_registry=set();cls._abc_cache=set();cls._abc_negative_cache=set();cls._abc_negative_cache_version=_abc_invalidation_counter
  cls._abc_registry.add(sub);global _abc_invalidation_counter;_abc_invalidation_counter+=1;return sub
 def __instancecheck__(cls,inst):
  return cls.__subclasscheck__(inst.__class__)
 def __subclasscheck__(cls,sub):
  if not hasattr(cls,'_abc_cache'):
   cls._abc_registry=set();cls._abc_cache=set();cls._abc_negative_cache=set();cls._abc_negative_cache_version=_abc_invalidation_counter
  if sub in cls._abc_cache:return True
  if cls in _abc_in_check:return False
  _abc_in_check.add(cls)
  try:
   if cls in getattr(sub,"__mro__",()):cls._abc_cache.add(sub);return True
   for r in cls._abc_registry:
    if issubclass(sub,r):cls._abc_cache.add(sub);return True
   return False
  finally:_abc_in_check.remove(cls)
class ABC(metaclass=ABCMeta):__slots__=()
def update_abstractmethods(c):return c
ABCMeta.__module__='abc'
