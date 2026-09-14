# Regression test: globals(), vars(module) and module.__dict__ are the
# module's namespace mapping and read and write through, as in CPython.
#
# A module is its own namespace in protoPython (module.__dict__ returns the
# module and globals() the frame's f_globals, which is the module), but only
# items/keys/values/update/copy were implemented on it:
# globals()['__name__'] was None, globals().get raised AttributeError and
# '__name__' in globals() was False.
import json

g = globals()
assert g['__name__'] == __name__, g['__name__']
assert g.get('__name__') == __name__
assert g.get('no_such_global_xyz') is None
assert g.get('no_such_global_xyz', 5) == 5
assert '__name__' in g
assert 'no_such_global_xyz' not in g
try:
    g['no_such_global_xyz']
except KeyError:
    pass
else:
    raise AssertionError("a missing key must raise KeyError")


def from_function():
    return globals()['__name__'], globals().get('from_function') is from_function


assert from_function() == (__name__, True)

assert vars(json)['__name__'] == 'json'
assert json.__dict__['__name__'] == 'json'
assert json.__dict__.get('dumps') is json.dumps
assert 'loads' in vars(json)


# Writes, deletes and the other dict methods act on the module.
def define():
    globals()['made_by_setitem'] = 42


define()
assert made_by_setitem == 42
assert 'made_by_setitem' in globals()
assert globals().setdefault('made_by_setitem', 0) == 42
assert globals().setdefault('made_by_setdefault', 7) == 7
assert made_by_setdefault == 7

vars(json)['_regression_marker'] = 'x'
assert json._regression_marker == 'x'
del json.__dict__['_regression_marker']
assert not hasattr(json, '_regression_marker')

temp_global = 1
del globals()['temp_global']
assert 'temp_global' not in globals()
try:
    temp_global
except NameError:
    pass
else:
    raise AssertionError("a deleted global is still bound")

popped = 3
assert globals().pop('popped') == 3
assert globals().pop('popped', 'gone') == 'gone'
assert 'popped' not in globals()

assert len(globals()) > 5
assert 'from_function' in list(globals())
assert dict(vars(json))['dumps'] is json.dumps

# exec() / eval() with a dict as globals.
ns = {}
exec("x = 1\ndef f():\n    return globals()['x']\ny = globals()['x']", ns)
assert ns['y'] == 1 and ns['f']() == 1
assert eval("globals()['x'] + 1", ns) == 2
exec("globals()['z'] = 5", ns)
assert ns['z'] == 5

print("module namespace mapping OK")
