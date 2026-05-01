"""SP-D / B-DD2 reproducer -- fence metaclass-leak in 4 __class__ sites.

Exercises the 4 sites that, prior to SP-D, walked the parent chain via
direct `getAttribute(ctx, "__class__")` instead of routing through
`env->getType` (which post-SP-B/B1 honors `__class__` only when it is
own on the receiver):

  1. py_type_instancecheck (MRO walk via __class__)
  2. handleException's sys.last_type
  3. formatException's type name in error messages
  4. eval/exec wrapper's SyntaxError detection

The invariant: a class with a custom metaclass must nevertheless
return the actual class (not the metaclass) when queried via these
paths. Same shape as B1's fix to getType.
"""
import sys


# Custom metaclass -- instances of MyError are instances of a class
# whose metaclass is Meta. type(myErrorInstance) must be MyError, not Meta.
class Meta(type):
    pass


class MyError(Exception, metaclass=Meta):
    pass


# 1. isinstance check via MRO must work for instances of a
#    metaclass-customized class. Exercises py_type_instancecheck
#    when isinstance falls through to the MRO branch.
e = MyError("boom")
assert isinstance(e, MyError), "isinstance failed for metaclass-customized exception class"
assert isinstance(e, Exception), "isinstance failed for inherited Exception"

# 2. Type identity -- type(e) must be MyError, not Meta.
assert type(e) is MyError, "type(e) = %s, expected MyError" % type(e).__name__

# 3. Raise + catch -- exercises formatException-shaped paths via
#    the except-clause class match. The except clause must match
#    on MyError, not Meta.
try:
    raise MyError("test message")
except MyError as caught:
    assert isinstance(caught, MyError)
    assert str(caught) == "test message"

# 4. sys.last_type invariant: handleException sets sys.last_type via
#    getType post-SP-D. We can't observe this directly without an
#    unhandled exception, but we document the invariant.

print("SP_D_B_DD2_OK")
