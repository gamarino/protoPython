# The ImportError raised by `from module import missing` has CPython's
# message, name and path; import errors carry no search-path suffix.
import json

try:
    from json import no_such_name_here
except ImportError as exc:
    expected = "cannot import name 'no_such_name_here' from 'json' (%s)" % json.__file__
    assert str(exc) == expected, (str(exc), expected)
    assert exc.name == "json", exc.name
    assert exc.path == json.__file__, (exc.path, json.__file__)
    assert type(exc) is ImportError
else:
    raise AssertionError("expected ImportError")

try:
    import no_such_module_protopython_test
except ImportError as exc:
    assert type(exc) is ModuleNotFoundError, type(exc)
    assert str(exc) == "No module named 'no_such_module_protopython_test'", str(exc)
    assert exc.name == "no_such_module_protopython_test", exc.name
else:
    raise AssertionError("expected ModuleNotFoundError")

print("import_from_error_message: ok")
