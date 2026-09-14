# Regression test: exec() and eval() run code objects from compile() in the
# right namespaces.
#
# py_exec / py_eval returned None for any non-str first argument, so
# exec(compile(src, name, "exec"), g) ran nothing and
# eval(compile(expr, name, "eval"), g) returned None.  Bare exec(src) inside
# a function also ignored the caller's locals, and eval(expr, globals) read
# the caller's locals instead of using globals as locals.
import types


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


code = compile("x = 40 + 2\ndef f():\n    return x\n", "<s>", "exec")
assert isinstance(code, types.CodeType), type(code)
g = {}
check("exec(code, globals) returns None", exec(code, g), None)
check("exec(code, globals) binds names", g["x"], 42)
check("function defined by exec(code) reads its globals", g["f"](), 42)
check("eval(code, globals)", eval(compile("x * 2", "<e>", "eval"), g), 84)

loc = {}
exec(compile("y = x + 1", "<s>", "exec"), g, loc)
check("exec(code, globals, locals) writes locals", loc, {"y": 43})
check("exec(code, globals, locals) leaves globals alone", "y" in g, False)

exec(compile("z = 5", "<s>", "exec"))
check("exec(code) without globals runs in the caller's module", z, 5)

exec(compile("s = 7", "<i>", "single"), g)
check("mode 'single'", g["s"], 7)

ns = {}
exec(compile("import math\nr = math.floor(2.5)", "<m>", "exec"), ns)
check("import inside exec(code)", ns["r"], 2)


def eval_code_sees_locals():
    a = 3
    return eval(compile("a * 2", "<e>", "eval"))


check("eval(code) without globals sees the caller's locals", eval_code_sees_locals(), 6)


def exec_sees_locals():
    a = 3
    out = {}
    exec("out['v'] = a * 2")
    return out["v"]


check("exec(str) without globals sees the caller's locals", exec_sees_locals(), 6)


def eval_explicit_globals():
    x = 1
    return eval("x", {"x": 2})


check("eval(expr, globals) does not read the caller's locals", eval_explicit_globals(), 2)

try:
    exec(compile("raise KeyError('boom')", "<r>", "exec"), {})
except KeyError as exc:
    raised = exc.args[0]
else:
    raised = None
check("exception raised by exec(code) propagates", raised, "boom")

print("exec code object OK")
