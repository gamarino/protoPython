# A bound method answers __module__ and __doc__ from its function, as
# CPython's method_getattro does; it used to answer the method type's own
# values ('builtins' and None).
import types


class A:
    def m(self):
        "doc of m"

    def plain(self):
        pass

    @classmethod
    def cm(cls):
        "doc of cm"


a = A()
assert a.m.__module__ == __name__, a.m.__module__
assert a.m.__doc__ == "doc of m", a.m.__doc__
assert a.plain.__doc__ is None
assert A.cm.__module__ == __name__, A.cm.__module__
assert A.cm.__doc__ == "doc of cm", A.cm.__doc__
assert a.cm.__doc__ == "doc of cm"

bm = types.MethodType(A.m, a)
assert bm.__module__ == __name__ and bm.__doc__ == "doc of m"

# getattr with a name built at run time takes the same path.
assert getattr(a.m, "__mod" + "ule__") == __name__
assert getattr(a.m, "__d" + "oc__") == "doc of m"

# The method type itself is unchanged.
assert types.MethodType.__module__ == "builtins"
print("bound_method_module_doc: ok")
