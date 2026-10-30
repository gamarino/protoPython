# StringIO / BytesIO methods called through the class with the instance as
# the first argument operate on that instance.  They read the class's
# empty default buffer, so doctest's _SpoofOut (getvalue via
# StringIO.getvalue(self), truncate via self.seek) lost every captured
# line and test_generators' doctests failed.
import doctest
import io


class Spoof(io.StringIO):
    def getvalue(self):
        return io.StringIO.getvalue(self) + "!"

    def truncate(self, size=None):
        self.seek(size)
        io.StringIO.truncate(self)


s = Spoof()
s.write("abc")
assert s.getvalue() == "abc!", s.getvalue()
s.truncate(1)
assert s.getvalue() == "a!", s.getvalue()

t = io.StringIO()
io.StringIO.write(t, "xy")
assert io.StringIO.getvalue(t) == "xy" and t.getvalue() == "xy"
io.StringIO.seek(t, 0)
assert io.StringIO.read(t) == "xy"

b = io.BytesIO()
io.BytesIO.write(b, b"q")
assert io.BytesIO.getvalue(b) == b"q"

out = doctest._SpoofOut()
out.write("line")
assert out.getvalue() == "line\n"


def documented():
    """
    >>> print(1 + 1)
    2
    """


finder = doctest.DocTestFinder()
runner = doctest.DocTestRunner(verbose=False)
for test in finder.find(documented, "documented", globs={}):
    runner.run(test, out=lambda _: None)
assert runner.failures == 0 and runner.tries == 1, (runner.failures, runner.tries)
print("stringio_unbound_methods: ok")
