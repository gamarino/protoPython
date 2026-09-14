"""Names assigned on the builtins module are visible to every lookup."""
import builtins
import sys

builtins.protopy_marker = 42
assert protopy_marker == 42


def read_marker():
    return protopy_marker


assert read_marker() == 42
builtins.protopy_marker = 43
assert read_marker() == 43 and protopy_marker == 43
assert hasattr(builtins, 'protopy_marker') and sys.modules['builtins'] is builtins
import builtins as again

assert again is builtins

original_len = builtins.len
builtins.len = lambda x: -1
try:
    assert len([1, 2]) == -1
finally:
    builtins.len = original_len
assert len([1, 2]) == 2


# A global set on a module from outside is seen by its functions, even after
# the name was looked up (and missing) before.
def read_late_global():
    try:
        return late_global
    except NameError:
        return None


assert read_late_global() is None
this_module = sys.modules[__name__]
this_module.late_global = 5
assert read_late_global() == 5
setattr(this_module, 'late_global', 6)
assert read_late_global() == 6

setattr(builtins, 'protopy_other', 'x')
assert protopy_other == 'x'
del builtins.protopy_marker
try:
    read_marker()
except NameError:
    pass
else:
    raise AssertionError("a deleted builtin is still visible")
print("builtins attributes OK")
