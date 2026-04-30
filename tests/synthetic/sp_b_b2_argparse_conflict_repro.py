# tests/synthetic/sp_b_b2_argparse_conflict_repro.py
"""SP-B / B2 reproducer — `'ArgumentParser' object has no attribute 'conflict_handler'`.

Surfaces in test_descr and test_re via the argparse import chain.  The
audit suggests a missing `__init__`-time attribute on the ArgumentParser
class — likely in ArgumentParser.__init__ which assigns
self.conflict_handler from the constructor argument.
"""
import argparse

p = argparse.ArgumentParser(prog="repro")
assert hasattr(p, "conflict_handler"), \
    f"ArgumentParser missing conflict_handler attr; dir(p)[:30]={dir(p)[:30]}"
assert p.conflict_handler == "error", \
    f"conflict_handler={p.conflict_handler!r}, expected 'error'"

print("SP_B_B2_OK")
