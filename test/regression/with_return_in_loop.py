# `return` from inside a loop that runs inside a `with` block.
#
# The loop's iterator sits on the operand stack above the context manager's
# __exit__.  Unwinding the with-block for the return has to drop the iterator
# first, otherwise OP_WITH_CLEANUP takes the iterator for __exit__ and calls
# it, which failed with "'object' object is not callable".
#
# The file opened here is this script itself, so the test writes nothing.

PATH = __file__
FIRST_LINE = "# `return` from inside a loop that runs inside a `with` block.\n"


def first_line_of_file():
    with open(PATH) as f:
        for line in f:
            return line


assert first_line_of_file() == FIRST_LINE, repr(first_line_of_file())


def first_item_of_list():
    with open(PATH) as f:
        for item in ["a", "b", "c"]:
            return item


assert first_item_of_list() == "a"


def bare_return():
    with open(PATH) as f:
        for _ in ["a", "b"]:
            return


assert bare_return() is None


def nested_loops():
    with open(PATH) as f:
        for a in [1, 2]:
            for b in [10, 20]:
                return a + b


assert nested_loops() == 11


def two_with_blocks():
    with open(PATH) as outer:
        with open(PATH) as inner:
            for line in inner:
                return line


assert two_with_blocks() == FIRST_LINE


def while_loop():
    with open(PATH) as f:
        n = 0
        while n < 3:
            n += 1
            return n


assert while_loop() == 1


# The context manager must still be exited when the return happens inside the
# loop: __exit__ has to run, with no exception reported to it.
exits = []


class Tracker:
    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        exits.append((exc_type, exc))
        return False


def return_from_tracked_loop():
    with Tracker() as t:
        for item in [11, 22, 33]:
            return item


assert return_from_tracked_loop() == 11
assert exits == [(None, None)], exits


# Nested managers both exit, innermost first.
order = []


class Named:
    def __init__(self, name):
        self.name = name

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        order.append(self.name)
        return False


def return_from_nested():
    with Named("outer"):
        with Named("inner"):
            for item in ["x", "y"]:
                return item


assert return_from_nested() == "x"
assert order == ["inner", "outer"], order


# A with-block return inside a try/finally keeps both cleanups.
order = []


def with_inside_finally():
    try:
        with open(PATH) as f:
            for line in f:
                return line
    finally:
        order.append("finally")


assert with_inside_finally() == FIRST_LINE
assert order == ["finally"]

print("with_return_in_loop: ok")
