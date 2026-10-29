# unittest.mock imports and its core behaviour works.  It used to fail to
# import: NonCallableMock defines `__class__` as a property for its
# instances, and protoPython took that class-namespace entry for the class's
# own metaclass ("metaclass conflict" at `class Mock(CallableMixin,
# NonCallableMock)`, reported as "No module named 'unittest.mock'").
import os
import unittest.mock as mock
from unittest.mock import MagicMock, Mock, call, patch, sentinel

m = Mock(return_value=3)
assert m(1, k=2) == 3
m.assert_called_once_with(1, k=2)
assert m.call_args == call(1, k=2)
assert m.call_count == 1

m = Mock(side_effect=[1, 2])
assert (m(), m()) == (1, 2)
m = Mock(side_effect=KeyError("k"))
try:
    m()
    raise AssertionError("side_effect exception not raised")
except KeyError:
    pass

m = Mock()
m.child.method(5)
assert m.mock_calls == [call.child.method(5)]
m.child.method.assert_called_with(5)
try:
    m.child.method.assert_called_with(6)
    raise AssertionError("assert_called_with accepted a wrong argument")
except AssertionError as e:
    assert "expected" in str(e).lower(), str(e)

mm = MagicMock()
mm.__len__.return_value = 4
assert len(mm) == 4
mm.__getitem__.return_value = "x"
assert mm[0] == "x"
with mm as entered:
    assert entered is mm.__enter__.return_value

assert sentinel.A is sentinel.A and sentinel.A is not sentinel.B

class Target:
    def value(self):
        return "real"

with patch.object(Target, "value", return_value="patched"):
    assert Target().value() == "patched"
assert Target().value() == "real"

with patch("os.getcwd", return_value="/nowhere"):
    assert os.getcwd() == "/nowhere"
assert os.getcwd() != "/nowhere"

@patch.object(Target, "value")
def decorated(fake):
    fake.return_value = 7
    return Target().value()

assert decorated() == 7

spec = Mock(spec=Target)
assert isinstance(spec, Target)
try:
    spec.not_there
    raise AssertionError("spec did not restrict attributes")
except AttributeError:
    pass
assert hasattr(mock, "AsyncMock")
print("unittest_mock_basics ok")
