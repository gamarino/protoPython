"""SP-D / B-DD2 reproducer -- upstream type-identity invariant fence.

This reproducer fences the upstream type-identity invariant from SP-B/B1:
for instances of metaclass-customized classes, `type(obj)`, except-clause
matching, and `isinstance(obj, cls)` must return the actual class, not the
metaclass.

The 5 sites routed through `env->getType` by SP-D (commit 1ced646b plus
the 5th-site follow-up at PythonEnvironment.cpp:12682) are *purely
defensive* locks against future regressions in the routing layer. They
were unreachable from Python user code pre-fix because:

  1. Every upstream caller (`type()` builtin, except-clause matching,
     OP_LOAD_NAME) already enters `getType`.
  2. Every instance allocated via `py_object_new` (and its tuple/list
     siblings) has its own `__class__` set at construction
     (PythonEnvironment.cpp:3160, 3253, 3272, 3338). With an OWN
     `__class__` present, the parent-chain `getAttribute("__class__")`
     walk used pre-fix would terminate on the OWN attribute and return
     the correct class — never reaching the parent-class's metaclass.

The pre-fix shape (parent-chain walk yielding the metaclass instead of
the class) requires constructing an instance without an OWN `__class__`,
which protopy's instance-creation pipeline does not produce from user
Python. Consequently this reproducer passes both pre-fix and post-fix
binaries; it asserts the upstream invariant holds, not that the routed
sites are reached. SP-D locks the invariant locally so it does not
depend on every caller staying upstream of `getType` forever.

Sites routed (all via `env->getType` instead of
`obj->getAttribute(ctx, "__class__")`):

  1. py_type_instancecheck            (~2788, isinstance MRO walk)
  2. handleException                  (~11845, sys.last_type)
  3. formatException                  (~11913, error-message type name)
  4. eval/exec wrapper                (~12196, SyntaxError detection)
  5. __getattribute__ AttributeError  (~12682, exc-class name filter)
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
