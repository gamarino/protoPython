# `import site` completes silently. It needed next(iterator, default) to
# return the default when __next__ raises StopIteration (site.venv() calls
# next(<generator expression>, None)), sys.copyright, and ImportError.name
# (site checks it after trying to import sitecustomize and usercustomize).
import io
import sys

captured = io.StringIO()
saved_stderr = sys.stderr
sys.stderr = captured
try:
    import site
finally:
    sys.stderr = saved_stderr
assert captured.getvalue() == "", captured.getvalue()
assert callable(site.getsitepackages)
assert isinstance(sys.copyright, str) and sys.copyright


# next() with a default.
def gen():
    yield 1


g = gen()
assert next(g) == 1
assert next(g, "default") == "default"
assert next((x for x in []), None) is None
assert next((x for x in [0, 1] if x), None) == 1
assert next(iter([]), "default") == "default"

try:
    next(g)
except StopIteration:
    pass
else:
    raise AssertionError("next() on an exhausted generator did not raise")


class Empty:
    def __iter__(self):
        return self

    def __next__(self):
        raise StopIteration


class Counter:
    def __init__(self):
        self.n = 0

    def __next__(self):
        self.n += 1
        return self.n


class Broken:
    def __next__(self):
        raise ValueError("broken")


assert next(Empty(), "default") == "default"
counter = Counter()
assert next(counter) == 1 and next(counter, "default") == 2

try:
    next(Empty())
except StopIteration:
    pass
else:
    raise AssertionError("next() without a default did not raise StopIteration")

# Only StopIteration selects the default.
try:
    next(Broken(), "default")
except ValueError:
    pass
else:
    raise AssertionError("next() swallowed a ValueError")

try:
    next([1])
except TypeError:
    pass
else:
    raise AssertionError("next() accepted a list")

# ImportError.name and path.
try:
    import nonexistent_module_for_import_site_test
except ModuleNotFoundError as exc:
    assert exc.name == "nonexistent_module_for_import_site_test", exc.name
else:
    raise AssertionError("importing a missing module did not raise")

try:
    from os import nonexistent_name_for_import_site_test
except ImportError as exc:
    assert exc.name == "os", exc.name
else:
    raise AssertionError("importing a missing name did not raise")

error = ImportError("message", name="pkg", path="/somewhere")
assert (error.name, error.path, error.args) == ("pkg", "/somewhere", ("message",))
assert ImportError("message").name is None
assert ImportError("message").path is None
assert ModuleNotFoundError("message", name="mod").name == "mod"

print("import_site: ok")
