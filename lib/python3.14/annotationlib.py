import enum
import typing

class Format(enum.IntEnum):
    VALUE = 1
    VALUE_WITH_FAKE_GLOBALS = 2
    FORWARDREF = 3
    STRING = 4

def get_annotations(obj, *, globals=None, locals=None, eval_str=False):
    return getattr(obj, "__annotations__", {})

def call_annotate_function(annotate, format, *, owner=None):
    return annotate(format)

def call_evaluate_function(evaluate, format, *, owner=None):
    return evaluate(format)
