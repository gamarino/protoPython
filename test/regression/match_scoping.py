# Regression test: `match` binds its capture names in the enclosing scope,
# hides its temporaries, and works inside loops.
#
# compileMatch stored the subject, its temporaries (`__match_subj`,
# `__match_tmp_<n>`) and the capture names with STORE_NAME / STORE_GLOBAL:
# inside a function the captures became module globals and the temporaries
# leaked into globals().  The built-in calls of a sequence pattern used the
# legacy call layout, so inside a `for` loop CALL_FUNCTION took the iterator
# for the callable: TypeError "'list_iterator' object is not callable".


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


def kind(v):
    match v:
        case [x, y]:
            return ("pair", x, y)
        case [x, *rest]:
            return ("head", x, rest)
        case {"k": value, **others}:
            return ("map", value, others)
        case int() if v > 10:
            return ("big", v)
        case 0 | 1 as small:
            return ("small", small)
        case str() as s:
            return ("str", s)
        case None:
            return ("none",)
        case _:
            return ("other",)


check("pair", kind([1, 2]), ("pair", 1, 2))
check("head", kind((1, 2, 3)), ("head", 1, [2, 3]))
check("map", kind({"k": 5, "z": 6}), ("map", 5, {"z": 6}))
check("small", kind(1), ("small", 1))
check("big", kind(11), ("big", 11))
check("str", kind("ab"), ("str", "ab"))
check("none", kind(None), ("none",))
check("other", kind(7), ("other",))
check("other float", kind(2.5), ("other",))

g = globals()
for name in ("x", "y", "rest", "value", "others", "n", "small", "s"):
    assert name not in g, "capture %r leaked into the module globals" % name
for name in list(g):
    assert not name.startswith("__match"), "temporary %r in the module globals" % name


def locals_after(v):
    match v:
        case [a, b]:
            pass
    return sorted(locals())


check("captures are function locals", locals_after([1, 2]), ["a", "b", "v"])


# A sequence pattern inside a loop (the iterator sits below the call).
out = []
for item in [[1, 2], [3], "xy", (4, 5)]:
    match item:
        case [p, q]:
            out.append((p, q))
        case [p]:
            out.append(p)
        case _:
            out.append(None)
check("sequence patterns in a module loop", out, [(1, 2), 3, None, (4, 5)])


def loop_in_function(items):
    res = []
    for it in items:
        match it:
            case {"op": "add", "args": [l, r]}:
                res.append(l + r)
            case [first, *_]:
                res.append(first)
            case _:
                res.append("?")
    return res


check("patterns in a function loop", loop_in_function([{"op": "add", "args": [2, 3]}, [9, 8], 4]),
      [5, 9, "?"])


def nested(a, b):
    match a:
        case [x]:
            match b:
                case [y]:
                    return x + y
                case _:
                    return -x
        case _:
            return 0


check("nested match", (nested([1], [2]), nested([1], 3), nested(5, [1])), (3, -1, 0))


def closure(v):
    match v:
        case [z]:
            return lambda: z
    return None


check("capture closed over", closure([42])(), 42)


class Point:
    __match_args__ = ("x", "y")

    def __init__(self, x, y):
        self.x = x
        self.y = y


def where(p):
    match p:
        case Point(0, 0):
            return "origin"
        case Point(x=0, y=yy):
            return "on y at %d" % yy
        case Point(px, py):
            return "at %d,%d" % (px, py)


check("class patterns", (where(Point(0, 0)), where(Point(0, 3)), where(Point(1, 2))),
      ("origin", "on y at 3", "at 1,2"))

# Module level: the captures are module names; no temporary is left.
pair = [10, 20]
match pair:
    case [mx, my]:
        pass
check("module-level captures", (mx, my), (10, 20))
for name in list(globals()):
    assert not name.startswith("__match"), "temporary %r in the module globals" % name


class Body:
    match 3:
        case cv:
            pass


check("class-body capture", Body.cv, 3)
assert not any(k.startswith("__match") for k in vars(Body)), "temporary in the class namespace"
print("match_scoping: ok")
