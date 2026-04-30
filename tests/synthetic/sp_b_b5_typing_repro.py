"""SP-B / B5 reproducer — typing.py:20 'NoneType' object is not callable.

The audit reports `typing` failing at line 20 (`from abc import abstractmethod, ABCMeta`)
with 'NoneType' object is not callable, and a related 'reraise outside of except block'
in the test_base64 import chain.  Both likely share root cause with the SP0 silent-halt
exception-machinery work.

This script triggers just the failing import and asserts both names are usable.
"""
from abc import abstractmethod, ABCMeta

# Verify both bindings landed
assert callable(abstractmethod), "abstractmethod is not callable"
assert isinstance(ABCMeta, type), \
    f"ABCMeta is {type(ABCMeta).__name__}, expected type"

# Sanity: ABCMeta should be usable as a metaclass
class _SP_B_B5_Probe(metaclass=ABCMeta):
    @abstractmethod
    def m(self): ...

assert isinstance(_SP_B_B5_Probe, ABCMeta)
print("SP_B_B5_OK")
