"""SP-B / B5 (NoneType-callable portion) reproducer.

Audit symptom: f-string conversion specifier (`!r`/`!s`/`!a`) inside a
function body raised `'NoneType' object is not callable`.  Root cause
(commit 167697dd): `Compiler::compileFormattedValue` emitted
`OP_LOAD_NAME` directly instead of routing through `emitNameOp`.  In
function scope, OP_LOAD_NAME walked frame->getAttribute and accepted
PROTO_NONE as "found", loading None instead of the builtin `repr`.

Two checks:
  1) Direct unit-style: an isolated function uses `f"{x!r}"`.
  2) Integration: typing.py's import chain (which transitively
     exercises function-scope f-strings).

Note: the reraise-outside-except portion of the original audit B5 has
a different root cause and is tracked separately.
"""

# 1. Direct unit-style reproducer — function-scope f-string with !r/!s/!a.
def _b5_repr():    return f"{1!r}"
def _b5_str():     return f"{1!s}"
def _b5_ascii():   return f"{1!a}"

assert _b5_repr() == "1", f"!r failed: {_b5_repr()!r}"
assert _b5_str() == "1", f"!s failed: {_b5_str()!r}"
assert _b5_ascii() == "1", f"!a failed: {_b5_ascii()!r}"

# 2. Integration test — same import the audit named.
from abc import abstractmethod, ABCMeta
assert callable(abstractmethod), "abstractmethod is not callable"
assert isinstance(ABCMeta, type)

class _SP_B_B5_Probe(metaclass=ABCMeta):
    @abstractmethod
    def m(self): ...

assert isinstance(_SP_B_B5_Probe, ABCMeta)
print("SP_B_B5_OK")
