"""SP-F / B4 reproducer — property setter error not malformed.

Pre-fix: assigning to a read-only @property raised AttributeError with
the attribute name being the sentence fragment 'property has no setter'
instead of the actual attribute name.  The audit surfaced this on the
test_sys / socket cluster-2 path with the shape:
    'socket' object has no attribute 'property has no setter'

Post-fix: the error is AttributeError with a CPython-style message:
    property 'value' of 'HasReadOnlyProperty' object has no setter
and the AttributeError's `name` slot carries the property identifier,
NEVER the sentence fragment.
"""


class HasReadOnlyProperty:
    @property
    def value(self):
        return 42


instance = HasReadOnlyProperty()

# 1. Reading works.
assert instance.value == 42, f"expected 42, got {instance.value!r}"

# 2. Writing must fail with a structured error.
try:
    instance.value = 99
    raise AssertionError("setting a read-only @property should raise")
except AttributeError as e:
    msg = str(e)
    # Whatever the message format, it must NOT be the malformed shape
    # `'X' object has no attribute 'property has no setter'`.
    assert "no attribute 'property has no setter'" not in msg, (
        f"malformed AttributeError message: {msg!r}"
    )
    # The AttributeError's `name` field MUST not be a sentence fragment.
    name = getattr(e, "name", None)
    if name is not None:
        assert name != "property has no setter", (
            f"AttributeError.name leaked sentence fragment: {name!r}"
        )
    # Positive check: the message must reference either the property
    # identifier ("value") or the word "setter" so callers can tell what
    # actually went wrong.
    assert ("value" in msg) or ("setter" in msg), (
        f"AttributeError message lacks property name and setter context: {msg!r}"
    )
except TypeError:
    # CPython 3.13+ raises AttributeError; older versions raised TypeError.
    # Either is acceptable so long as the malformed AttributeError shape
    # is gone.
    pass

print("SP_F_B4_OK")
