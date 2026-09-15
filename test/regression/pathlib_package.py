# `import pathlib` loads the standard library package. A native stub
# registered under the same name shadowed it: pathlib.Path was a plain object
# that could not be called. The package also needs __slots__ attributes read
# inside property getters to raise AttributeError when unset, and native os
# functions to accept os.PathLike arguments.
import errno
import os
import pathlib

p = pathlib.Path("a") / "b"
assert str(p) == os.path.join("a", "b")
assert repr(p) == "PosixPath('a/b')"
assert p.name == "b"
assert p.parent.name == "a"
assert os.fspath(p) == os.path.join("a", "b")
assert pathlib.Path("dir/file.txt").suffix == ".txt"
assert pathlib.Path("dir/file.txt").stem == "file"
assert pathlib.PurePosixPath("/usr/lib").parts == ("/", "usr", "lib")
assert isinstance(p, pathlib.PurePath)

here = pathlib.Path(__file__)
missing = pathlib.Path(__file__ + ".missing")
assert here.exists() and here.is_file() and not here.is_dir()
assert here.parent.is_dir()
assert not missing.exists() and not missing.is_file()
regression_dir = pathlib.Path(__file__).parent
assert here.name in [child.name for child in regression_dir.iterdir()]


# Native os functions accept any os.PathLike.
class PathLike:
    def __init__(self, path):
        self.path = path

    def __fspath__(self):
        return self.path


assert os.stat(here).st_size == os.stat(__file__).st_size
assert os.path.isfile(PathLike(__file__))
assert not os.path.exists(PathLike(__file__ + ".missing"))
try:
    os.stat(missing)
except OSError as exc:
    assert exc.errno == errno.ENOENT, exc.errno
else:
    raise AssertionError("os.stat() of a missing pathlib.Path did not raise")


# An unset __slots__ attribute read inside a property getter raises
# AttributeError, which the getter can catch to fill a cache.
class Cached:
    __slots__ = ("_value",)

    @property
    def value(self):
        try:
            return self._value
        except AttributeError:
            self._value = 42
            return self._value

    @property
    def unset(self):
        return self._value


assert Cached().value == 42
try:
    Cached().unset
except AttributeError:
    pass
else:
    raise AssertionError("reading an unset slot inside a property did not raise")

print("pathlib_package: ok")
