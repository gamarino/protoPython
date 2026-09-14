"""NotImplemented and Ellipsis print and repr as their names."""
assert repr(NotImplemented) == "NotImplemented" and str(NotImplemented) == "NotImplemented"
assert repr(Ellipsis) == "Ellipsis" and str(...) == "Ellipsis"
assert f"{NotImplemented} {Ellipsis}" == "NotImplemented Ellipsis"
assert "%s %r" % (NotImplemented, ...) == "NotImplemented Ellipsis"
assert repr([NotImplemented, ...]) == "[NotImplemented, Ellipsis]"
print(NotImplemented, Ellipsis)
print("notimplemented repr OK")
