# Regression test: sys.setswitchinterval / sys.getswitchinterval exist and
# validate their argument like CPython. protoPython has no GIL, so the value
# has no scheduling effect, but code that tunes it must keep working.
import sys

assert sys.getswitchinterval() == 0.005, "default switch interval: %r" % (sys.getswitchinterval(),)

sys.setswitchinterval(0.001)
assert sys.getswitchinterval() == 0.001, "after setswitchinterval(0.001): %r" % (sys.getswitchinterval(),)

sys.setswitchinterval(2)
assert sys.getswitchinterval() == 2.0, "after setswitchinterval(2): %r" % (sys.getswitchinterval(),)

sys.setswitchinterval(True)
assert sys.getswitchinterval() == 1.0, "after setswitchinterval(True): %r" % (sys.getswitchinterval(),)

for bad in (0, -1, 0.0, -0.5, False):
    try:
        sys.setswitchinterval(bad)
    except ValueError:
        pass
    else:
        raise AssertionError("setswitchinterval(%r) did not raise ValueError" % (bad,))

try:
    sys.setswitchinterval("fast")
except TypeError:
    pass
else:
    raise AssertionError("setswitchinterval('fast') did not raise TypeError")

assert sys.getswitchinterval() == 1.0, "rejected values changed the interval"
sys.setswitchinterval(0.005)
print("sys switchinterval OK")
