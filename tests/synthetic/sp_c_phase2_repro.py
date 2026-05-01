"""SP-C / Phase 2 reproducer — import inspect succeeds after C1.

If Task 1 broke `import inspect`, Task 2 fixes the underlying cause.
"""
import inspect

assert hasattr(inspect, "signature"), "inspect.signature missing"
assert hasattr(inspect, "isclass"), "inspect.isclass missing"
print("SP_C_PHASE2_OK")
