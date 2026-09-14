"""A module's globals are its namespace, not the attributes every module inherits."""
import json


def name_error(fn):
    try:
        fn()
    except NameError:
        return True
    return False


for name in ("keys", "items", "get", "pop", "update", "__init__", "__repr__",
             "__getitem__", "__class__"):
    assert name_error(lambda: eval(name)), name


def class_from_function():
    return __class__


def keys_from_function():
    return keys


assert name_error(class_from_function) and name_error(keys_from_function)


class C:
    def method(self):
        return __class__


assert C().method() is C
assert isinstance(__name__, str) and json.dumps([1]) == "[1]"
assert globals().get('json') is json and 'C' in globals()

keys = "mine"
assert keys == "mine" and keys_from_function() == "mine" and globals()['keys'] == "mine"
print("module global names OK")
