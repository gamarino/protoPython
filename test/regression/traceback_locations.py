"""Traceback entries carry the running code object's file name, function name
and the line being executed, for functions, modules, imported modules and
exec/eval/compile code (CPython's shape: oldest call first)."""
import os
import sys
import tempfile
import traceback

HERE = os.path.basename(__file__)


def summary(exc):
    """[(file basename, function, line)] of exc's traceback."""
    return [(os.path.basename(fs.filename), fs.name, fs.lineno)
            for fs in traceback.extract_tb(exc.__traceback__)]


def catch(fn):
    try:
        fn()                                    # @catch
    except Exception as e:                      # noqa: BLE001
        return e
    raise AssertionError("no exception")


def lines_of(names):
    """{name: 1-based line number of the line ending in '# @name'} in this file."""
    with open(__file__) as f:
        src = f.read().splitlines()
    return {name: i + 1 for i, line in enumerate(src)
            for name in names if line.rstrip().endswith('# @' + name)}


L = lines_of(['inner', 'outer', 'catch', 'mod_raise', 'loop_raise'])


def inner():
    x = 1
    y = x - 1
    return x / y                                # @inner


def outer():
    a = 2
    return inner() + a                          # @outer


# 1. Nested function calls: one entry per frame, each with its own line.
e = catch(outer)
assert isinstance(e, ZeroDivisionError)
s = summary(e)
assert s == [(HERE, 'catch', L['catch']), (HERE, 'outer', L['outer']),
             (HERE, 'inner', L['inner'])], s

# 2. The line is the one executing, not the function's first line, also
#    inside loops and after jumps.
def in_loop():
    total = 0
    for i in range(5):
        total += i
        if i == 3:
            raise ValueError(total)             # @loop_raise
    return total


s = summary(catch(in_loop))
assert s[-1] == (HERE, 'in_loop', L['loop_raise']), s

# 3. Module level: the frame is '<module>' and the line is the raising one.
try:
    {}['missing']                               # @mod_raise
except KeyError as e:
    s = summary(e)
assert s == [(HERE, '<module>', L['mod_raise'])], s

# 4. A function of an imported module reports that module's file.
tmp = tempfile.mkdtemp()
with open(os.path.join(tmp, 'tb_helper_mod.py'), 'w') as f:
    f.write("def boom(n):\n"
            "    m = n + 1\n"
            "    raise RuntimeError(m)\n"
            "\n"
            "def call_boom():\n"
            "    return boom(1)\n")
sys.path.insert(0, tmp)
import tb_helper_mod                            # noqa: E402

s = summary(catch(tb_helper_mod.call_boom))
assert s[1:] == [('tb_helper_mod.py', 'call_boom', 6),
                 ('tb_helper_mod.py', 'boom', 3)], s

# 5. exec / eval of a string report '<string>' and the line inside it.
s = summary(catch(lambda: exec("a = 1\nb = 0\nc = a / b\n")))
assert s[-1] == ('<string>', '<module>', 3), s
s = summary(catch(lambda: eval("1 +\\\n undefined_name")))
assert s[-1][0] == '<string>' and s[-1][1] == '<module>', s

# 6. compile() with an explicit file name, code that defines and calls a function.
code = compile("def h():\n    raise KeyError('k')\nh()\n", "<mycode>", "exec")
s = summary(catch(lambda: exec(code, {})))
assert s[-2:] == [('<mycode>', '<module>', 3), ('<mycode>', 'h', 2)], s

# 7. format_exc has CPython's layout: header, a File line per frame, and the
#    exception last. (Source lines under each entry come from linecache, which
#    reads through tokenize.open; read-mode file objects have no seek() yet,
#    so they are not checked here. The uncaught-exception printer shows them:
#    see traceback_uncaught.py.)
try:
    outer()
except ZeroDivisionError:
    text = traceback.format_exc()
lines = text.splitlines()
assert lines[0] == 'Traceback (most recent call last):', lines
assert lines[-1] == 'ZeroDivisionError: division by zero', lines
file_lines = [ln for ln in lines if ln.startswith('  File ')]
assert len(file_lines) == 3, lines
assert file_lines[-1] == '  File "%s", line %d, in inner' % (__file__, L['inner']), file_lines
assert '<unknown>' not in text, text

# 8. Chained frames walk tb_next with real frame objects.
tb = catch(outer).__traceback__
names = []
while tb is not None:
    assert tb.tb_frame.f_code.co_filename == __file__
    assert isinstance(tb.tb_lineno, int) and tb.tb_lineno > 0
    names.append(tb.tb_frame.f_code.co_name)
    tb = tb.tb_next
assert names == ['catch', 'outer', 'inner'], names
print("traceback locations OK")
