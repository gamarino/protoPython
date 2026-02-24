code = "lambda _cls, hits, misses, maxsize, currsize: _tuple_new(_cls, (hits, misses, maxsize, currsize))"
namespace = {
    "_tuple_new": tuple.__new__,
    "__builtins__": {},
    "__name__": "namedtuple_CacheInfo"
}
print("Before eval")
__new__ = eval(code, namespace)
print("After eval, __new__ is:", __new__)
try:
    __new__.__name__ = '__new__'
    print("set __name__")
except Exception as e:
    print("Exception setting __name__:", type(e), e)
