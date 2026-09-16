# A directory added to sys.path at runtime is searched by import.
#
# The module provider walked only the search paths captured when the
# environment was created, so `sys.path.insert(0, d)` had no effect and the
# import raised ModuleNotFoundError.
#
# The directory used is the sys_path_fixture/ subdirectory next to this file.
# It is not a search path of its own, so the fixture module is unreachable
# until sys.path names it; the test writes nothing.

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURE_DIR = os.path.join(HERE, "sys_path_fixture")
MODULE = "sys_path_runtime_insert_target"

assert os.path.exists(os.path.join(FIXTURE_DIR, MODULE + ".py")), \
    "missing fixture in %s" % FIXTURE_DIR
assert FIXTURE_DIR not in sys.path, "test precondition failed: fixture dir already on sys.path"

# Unreachable before the directory is added.
try:
    __import__(MODULE)
    raise AssertionError("%s was importable before its directory was added" % MODULE)
except ImportError:
    pass

# insert() is picked up.
sys.path.insert(0, FIXTURE_DIR)
try:
    mod = __import__(MODULE)
    assert mod.VALUE == 4242, mod.VALUE
    assert mod.identity(7) == 7
    assert sys.path[0] == FIXTURE_DIR
    assert MODULE in sys.modules
finally:
    sys.path.remove(FIXTURE_DIR)
    sys.modules.pop(MODULE, None)

# Note: the module stays importable from here on. protoPython keeps loaded
# modules in a ProtoSpace-wide cache that sys.modules does not control, so
# dropping the sys.path entry does not unload it. What matters for this test is
# the transition above: unreachable before the entry existed, reachable after.

# append() reaches the directory as well as insert().
sys.modules.pop(MODULE, None)
sys.path.append(FIXTURE_DIR)
try:
    mod = __import__(MODULE)
    assert mod.VALUE == 4242
    assert sys.path[-1] == FIXTURE_DIR
finally:
    sys.path.remove(FIXTURE_DIR)
    sys.modules.pop(MODULE, None)

# The standard library is still importable after sys.path churn.
import json

assert json.dumps({"a": 1}) == '{"a": 1}'

print("sys_path_runtime_insert: ok")
