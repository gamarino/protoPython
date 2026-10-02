# PEP 649 / 749: a function with annotations has an __annotate__ function; one
# without has __annotate__ None. functools.singledispatch's plain `@register`
# reads the dispatch type through __annotate__ (it used to raise "Invalid first
# argument to `register()`" because __annotate__ was always None).
#
# protoPython evaluates annotations when the function is defined, so
# __annotate__ returns them for Format.VALUE and Format.FORWARDREF, renders
# them with annotationlib.annotations_to_string for Format.STRING, and raises
# NotImplementedError for VALUE_WITH_FAKE_GLOBALS. CPython's compiler-generated
# __annotate__ supports VALUE only and annotationlib.call_annotate_function
# derives the other formats; through call_annotate_function both runtimes
# give the same results, which is what this test checks.
import annotationlib
import functools
from annotationlib import Format


def annotated(a: int, b: "str" = "x") -> list:
    pass


def plain(a, b=1):
    pass


expected = {"a": int, "b": "str", "return": list}
assert plain.__annotate__ is None, plain.__annotate__
assert plain.__annotations__ == {}, plain.__annotations__
assert callable(annotated.__annotate__), annotated.__annotate__
assert annotated.__annotate__(Format.VALUE) == expected, annotated.__annotate__(Format.VALUE)
call = annotationlib.call_annotate_function
assert call(annotated.__annotate__, Format.VALUE) == expected
assert call(annotated.__annotate__, Format.FORWARDREF) == expected
assert call(annotated.__annotate__, Format.STRING) == {"a": "int", "b": "str", "return": "list"}, \
    call(annotated.__annotate__, Format.STRING)
assert annotated.__annotate__(Format.VALUE) is not annotated.__annotate__(Format.VALUE), \
    "each call returns a new dict"
assert annotated.__annotations__ == expected
for fmt in (Format.VALUE, Format.FORWARDREF):
    assert annotationlib.get_annotations(annotated, format=fmt) == expected, fmt
assert annotationlib.get_annotations(annotated, format=Format.STRING) == \
    {"a": "int", "b": "str", "return": "list"}


@functools.singledispatch
def kind(x):
    return "object"


@kind.register
def _(x: int):
    return "int"


@kind.register
def _(x: str) -> str:
    return "str"


@kind.register
def _(x: list | tuple):
    return "sequence"


assert kind(1) == "int", kind(1)
assert kind("a") == "str"
assert kind([1]) == "sequence" and kind((1,)) == "sequence"
assert kind(1.5) == "object"


class Shape:
    @functools.singledispatchmethod
    def area(self, arg):
        return None

    @area.register
    def _(self, arg: int):
        return arg * arg


assert Shape().area(3) == 9
assert Shape().area("x") is None

try:
    @kind.register
    def _(x):
        return "unannotated"
except TypeError as e:
    assert "annotated function" in str(e), e
else:
    raise AssertionError("register() accepted a function without annotations")
print("function_annotate: ok")
