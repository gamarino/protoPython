# Regression test: `del name` unbinds the name in module, class and global scope.
#
# OP_DELETE_NAME / OP_DELETE_GLOBAL discarded the SparseList returned by
# removeAt and never touched the frame's own attributes or __keys__, so every
# `del` was a no-op: `class C: x = 1; del x` left C.x, a deleted module global
# stayed readable, and deleting an unbound name raised nothing.


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


def raises_name_error(fn):
    try:
        fn()
    except NameError:
        return True
    return False


class C:
    x = 1
    y = 2
    del x


check("class del removes attribute", hasattr(C, "x"), False)
check("class del keeps others", C.y, 2)
check("class del not in __dict__", "x" in C.__dict__, False)


class D:
    tmp = [1, 2]
    total = sum(tmp)
    del tmp


check("class helper deleted", hasattr(D, "tmp"), False)
check("class helper result kept", D.total, 3)


def make():
    class E:
        a = 1
        del a
    return E


check("nested class del", hasattr(make(), "a"), False)

caught = None


class F:
    try:
        del missing
    except NameError:
        caught = True


check("class del of unbound name raises NameError", F.caught, True)


class G:
    v = 1
    del v
    visible = True
    try:
        v
    except NameError:
        visible = False


check("deleted class name not visible in body", G.visible, False)


class NS(dict):
    deleted = []

    def __delitem__(self, key):
        NS.deleted.append(key)
        dict.__delitem__(self, key)


class Meta(type):
    @classmethod
    def __prepare__(mcls, name, bases, **kwargs):
        return NS()


class H(metaclass=Meta):
    b = 1
    del b


check("__prepare__ namespace __delitem__ called", NS.deleted, ["b"])
check("__prepare__ namespace entry removed", "b" in H.__dict__, False)

m = 1
del m
check("module del", raises_name_error(lambda: m), True)
check("module del of unbound name", raises_name_error(lambda: exec("del never_bound_name", {})), True)


def set_and_del_global():
    global gz
    gz = 5
    del gz


set_and_del_global()
check("global del", "gz" in globals(), False)
check("global del unbinds", raises_name_error(lambda: gz), True)

ns = {}
exec("a = 1\ndel a", ns)
check("exec dict namespace del", "a" in ns, False)

print("del name OK")
