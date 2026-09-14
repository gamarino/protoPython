# Regression test: compile(src, name, "single") echoes expression values.
#
# compile() compiled "single" like "exec": expression statements were popped,
# so nothing reached sys.displayhook, and the default hook was a no-op that
# neither printed nor set builtins._.  "single" also accepted several
# statements, which CPython rejects.


def check(label, got, expected):
    assert got == expected, "%s: got %r, expected %r" % (label, got, expected)


import sys


class Capture:
    def __init__(self):
        self.parts = []

    def write(self, text):
        self.parts.append(text)
        return len(text)

    def flush(self):
        pass


def run(src, ns, mode="single"):
    code = compile(src, "<single>", mode)
    cap = Capture()
    saved = sys.stdout
    sys.stdout = cap
    try:
        exec(code, ns)
    finally:
        sys.stdout = saved
    return "".join(cap.parts)


ns = {}
check("expression is echoed", run("1 + 1", ns), "2\n")
check("repr is used", run("'s'", ns), "'s'\n")
check("_ holds the last value", eval("_"), "s")
check("builtins._ holds the last value", __import__("builtins")._, "s")
check("None is not echoed", run("None", ns), "")
check("None keeps _", eval("_"), "s")
check("assignment prints nothing", run("x = 5", ns), "")
check("assignment binds", ns["x"], 5)
check("name is echoed", run("x", ns), "5\n")
check("simple statements on one line", run("y = 2; y * 3", ns), "6\n")
check("expressions in compound statements", run("for i in range(2): i\n", ns), "0\n1\n")
check("function bodies are not echoed", run("def f():\n    42\n\n", ns), "")
check("a None result is not echoed", run("f()", ns), "")


class R:
    def __repr__(self):
        return "<R>"


ns["R"] = R
check("user __repr__", run("R()", ns), "<R>\n")
check("exec mode does not echo", run("1 + 1", ns, "exec"), "")

seen = []
sys.displayhook = seen.append
try:
    exec(compile("40 + 2", "<single>", "single"), {})
    exec(compile("None", "<single>", "single"), {})
finally:
    sys.displayhook = sys.__displayhook__
check("custom displayhook receives every value", seen, [42, None])


def syntax_error(src):
    try:
        compile(src, "<single>", "single")
    except SyntaxError:
        return True
    return False


check("two statements", syntax_error("1\n2"), True)
check("statement after a compound statement", syntax_error("if 1:\n    pass\nx = 1\n"), True)
check("statement after a loop", syntax_error("for i in ():\n    pass\ny = 1\n"), True)
check("one compound statement", syntax_error("if 1:\n    pass\n"), False)
check("one-line compound statement with ;", syntax_error("if 1: pass; z = 1\n"), False)
check("if / else is one statement", syntax_error("if 0:\n    pass\nelse:\n    pass\n"), False)

print("compile single OK")
