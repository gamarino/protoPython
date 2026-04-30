"""SP-B / B1 reproducer — `'ABCMeta' object has no attribute 'gen'`.

Surfaces in test_contextlib via `_GeneratorContextManagerBase()` and in
test_asyncgen via similar generator-context-manager paths.  This
reproducer triggers the symptom in isolation by calling the public
contextlib.contextmanager API (which constructs a
_GeneratorContextManagerBase under the hood).
"""
from contextlib import contextmanager

@contextmanager
def my_ctx():
    yield 42

with my_ctx() as v:
    assert v == 42

print("SP_B_B1_OK")
