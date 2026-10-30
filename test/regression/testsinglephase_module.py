# _testsinglephase exists, as in every standard CPython build:
# test.test_importlib.util imports it at module level and raised SkipTest
# without it, which skipped unittest.mock's testpatch as a whole.
import _testsinglephase as m

assert m.int_const == 1969
assert m.str_const == "something different"
assert issubclass(m.error, Exception) and m.error.__module__ == "_testsinglephase"
assert m.sum(2, 3) == 5
assert m.look_up_self() is m
assert m.initialized_count() == 1
assert isinstance(m._module_initialized, float)
assert isinstance(m.state_initialized(), float)

import test.test_importlib.util as util  # no SkipTest
assert callable(util.uncache)
print("testsinglephase_module: ok")
