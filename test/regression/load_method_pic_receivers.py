# Regression test: the LOAD_METHOD inline cache must not reuse a receiver.
#
# The (type, name) polymorphic inline cache used to store the bound
# native method produced for the first receiver, so later calls on any
# other instance of the same type ran on that first receiver:
# `b.append(x)` appended to `a`, and every `_thread` lock resolved to a
# single lock (deadlocking threading.Thread.start()).
import _thread


def call_append(lst, v):
    lst.append(v)


a = []
b = []
call_append(a, 1)
call_append(b, 2)
assert a == [1] and b == [2], (a, b)


def call_get(d, k):
    return d.get(k)


assert call_get({"x": 1}, "x") == 1
assert call_get({"x": 2}, "x") == 2


def call_upper(s):
    return s.upper()


assert call_upper("abc") == "ABC"
assert call_upper("xyz") == "XYZ"


class Box:
    def __init__(self, v):
        self.v = v

    def get(self):
        return self.v

    @property
    def getter(self):
        return lambda: self.v


def call_get_method(o):
    return o.get()


def call_property_callable(o):
    return o.getter()


assert call_get_method(Box(1)) == 1
assert call_get_method(Box(2)) == 2
assert call_property_callable(Box(1)) == 1
assert call_property_callable(Box(2)) == 2


def call_locked(lock):
    return lock.locked()


first = _thread.allocate_lock()
second = _thread.allocate_lock()
first.acquire()
assert call_locked(first) is True
assert call_locked(second) is False
assert second.acquire(False) is True
second.release()
first.release()


def call_acquire(lock):
    return lock.acquire(False)


plain = _thread.allocate_lock()
rlock = _thread.RLock()
assert call_acquire(plain) is True
assert call_acquire(rlock) is True
assert plain.locked() is True
plain.release()
rlock.release()

print("load_method PIC receivers OK")
