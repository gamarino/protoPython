"""A bare module name is a NameError until imported; __dict__ is not a global."""


def name_error(src):
    try:
        eval(src)
    except NameError:
        return True
    return False


for name in ("json", "copy", "string", "textwrap", "__dict__"):
    assert name_error(name), name


def eval_in_function(src):
    try:
        eval(src)
    except NameError:
        return True
    return False


# eval's locals inside a function, and a class body, only see names.
assert eval_in_function("copy") and eval_in_function("keys")


class Body:
    try:
        __init__
        init_visible = True
    except NameError:
        init_visible = False
    try:
        __dict__
        dict_visible = True
    except NameError:
        dict_visible = False
    flag = False
    flag_seen = flag


assert not Body.init_visible and not Body.dict_visible and Body.flag_seen is False


def from_function():
    return textwrap


try:
    from_function()
except NameError:
    pass
else:
    raise AssertionError("textwrap resolved without an import")

import textwrap

assert from_function() is textwrap and eval("textwrap") is textwrap
import os.path

assert os.path.join("a", "b") == "a/b" and os is __import__("os")
print("global names no import OK")
