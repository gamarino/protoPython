# Regression: function.__closure__ is None or a tuple of cells, and
# types.CellType is the type of those cells.
# protoPython stored the captured-variables frame itself as the only
# element of `__closure__`, so `f.__closure__[0]` was a frame,
# types.CellType was the frame type and `cell_contents` did not exist.
import types


def make():
    x = 1
    return (lambda: x).__closure__[0]


cell = make()
assert type(cell) is types.CellType, type(cell)
assert cell.cell_contents == 1
assert isinstance(cell, types.CellType)
assert types.CellType.__name__ == 'cell'
assert types.CellType is not types.FrameType


def no_free():
    return 1


assert no_free.__closure__ is None


def outer():
    a = 10
    b = 'two'
    def inner():
        return a, b
    return inner


inner = outer()
assert type(inner.__closure__) is tuple
assert inner.__code__.co_freevars == ('a', 'b')
assert [c.cell_contents for c in inner.__closure__] == [10, 'two']


# Writing through a cell is seen by the closure (dataclasses relies on
# this to repoint the __class__ cell of a recreated slots class).
def reader():
    v = 'old'
    def get():
        return v
    return get


get = reader()
c = get.__closure__[0]
c.cell_contents = 'new'
assert c.cell_contents == 'new'
assert get() == 'new'


def recursive():
    def fact(n):
        return 1 if n <= 1 else n * fact(n - 1)
    return fact


fact = recursive()
assert fact(5) == 120
assert fact.__code__.co_freevars == ('fact',)
assert fact.__closure__[0].cell_contents is fact


# Standalone cells, as annotationlib and pdb build them.
standalone = types.CellType(42)
assert standalone.cell_contents == 42
standalone.cell_contents = 43
assert standalone.cell_contents == 43
empty = types.CellType()
try:
    empty.cell_contents
except ValueError:
    pass
else:
    raise AssertionError("empty cell did not raise ValueError")

# Lambdas close over variables like defs (their code objects carry
# co_freevars too).
def lam():
    y = 'captured'
    return lambda: y


f = lam()
assert f.__code__.co_freevars == ('y',)
assert f.__closure__[0].cell_contents == 'captured'
# __closure__ is read-only, as in CPython.
try:
    f.__closure__ = None
except (AttributeError, TypeError):
    pass
else:
    raise AssertionError("__closure__ was assignable")

print("closure_cells OK")
