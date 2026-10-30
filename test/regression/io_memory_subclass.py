# io.StringIO and io.BytesIO are real classes: they can be subclassed and
# the subclass inherits their methods.  doctest's _SpoofOut subclasses
# StringIO and failed in every doctest ("no attribute 'seek'").
import io


class S(io.StringIO):
    def getvalue(self):
        return "<" + super().getvalue() + ">"


assert type(io.StringIO) is type and type(io.BytesIO) is type
assert S.__mro__[0] is S and io.StringIO in S.__mro__ and S.__mro__[-1] is object
assert io.StringIO.__mro__[0] is io.StringIO
s = S()
s.write("abc")
assert s.getvalue() == "<abc>", s.getvalue()
s.seek(0)
s.truncate(0)
assert s.getvalue() == "<>"
assert isinstance(s, io.StringIO)
assert S("xy").read() == "xy"
assert io.StringIO("q").getvalue() == "q"


class B(io.BytesIO):
    pass


b = B(b"12")
b.seek(0, 2)
b.write(b"3")
assert b.getvalue() == b"123" and isinstance(b, io.BytesIO)
print("io_memory_subclass: ok")
