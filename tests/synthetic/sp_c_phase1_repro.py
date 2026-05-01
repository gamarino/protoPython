"""SP-C / Phase 1 reproducer -- `in cls.__dict__` returns own-only.

Pre-fix: 'x' (own) returned False, '__init__' (inherited) returned True
because the in-operator bypassed __contains__ and probed the class's
full attribute storage via asSparseList.
"""
class P:
    x = 1

d = P.__dict__
assert 'x' in d, "'x' should be in cls.__dict__ (own attr)"
assert '__init__' not in d, "'__init__' should NOT be in cls.__dict__ (inherited)"
assert '__class__' in d, "'__class__' should be in cls.__dict__ (own)"
assert 'nonexistent' not in d

print("SP_C_PHASE1_OK")
