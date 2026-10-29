# A MagicMock is iterable through its configured __iter__ (unittest.mock
# installs the magic methods on the mock's own subclass).  iter(),
# for-loops, list() and `in` used to raise TypeError.
from unittest.mock import MagicMock

m = MagicMock()
assert list(m) == [], list(m)
m.__iter__.return_value = iter([1, 2, 3])
assert [x for x in m] == [1, 2, 3]
m.__iter__.return_value = [4, 5]
assert list(iter(m)) == [4, 5]
assert m.__iter__.call_count == 3, m.__iter__.call_count
m2 = MagicMock()
m2.__contains__.return_value = True
assert 7 in m2
assert len(m2) == 0
m2.__len__.return_value = 4
assert len(m2) == 4
print("magicmock_iteration: ok")
