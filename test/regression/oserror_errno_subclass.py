# OS errors raise the OSError subclass that matches their errno, and
# OSError(errno, strerror[, filename]) builds that subclass, with errno,
# strerror and filename set, as in CPython.
import errno
import os
import tempfile


def raises(exc_type, fn, err, filename):
    try:
        fn()
    except OSError as exc:
        assert type(exc) is exc_type, (type(exc), exc_type)
        assert exc.errno == err, exc.errno
        assert exc.strerror == os.strerror(err), exc.strerror
        assert exc.filename == filename, exc.filename
        assert exc.args == (err, os.strerror(err)), exc.args
        assert str(exc) == "[Errno %d] %s: %r" % (err, os.strerror(err), filename), str(exc)
    else:
        raise AssertionError("expected %s" % exc_type.__name__)


missing = "/nonexistent-protopython-dir/file"
raises(FileNotFoundError, lambda: os.stat(missing), errno.ENOENT, missing)
raises(FileNotFoundError, lambda: open(missing), errno.ENOENT, missing)
raises(FileNotFoundError, lambda: os.listdir(missing), errno.ENOENT, missing)
raises(IsADirectoryError, lambda: open("/"), errno.EISDIR, "/")

base = tempfile.mkdtemp()
try:
    path = os.path.join(base, "f")
    os.close(os.open(path, os.O_CREAT | os.O_WRONLY, 0o600))
    raises(NotADirectoryError, lambda: os.listdir(path), errno.ENOTDIR, path)
    raises(FileExistsError, lambda: os.mkdir(base), errno.EEXIST, base)
    raises(IsADirectoryError, lambda: os.remove(base), errno.EISDIR, base)
    os.remove(path)
finally:
    os.rmdir(base)

# except clauses for the base class still catch the subclasses.
try:
    os.stat(missing)
except OSError as exc:
    assert isinstance(exc, FileNotFoundError)

# Direct construction.
exc = OSError(errno.ENOENT, "No such file", "name")
assert type(exc) is FileNotFoundError
assert (exc.errno, exc.strerror, exc.filename) == (errno.ENOENT, "No such file", "name")
assert exc.args == (errno.ENOENT, "No such file")
assert str(exc) == "[Errno 2] No such file: 'name'", str(exc)

exc = OSError(errno.EACCES, "denied")
assert type(exc) is PermissionError
assert (exc.errno, exc.strerror, exc.filename) == (errno.EACCES, "denied", None)
assert str(exc) == "[Errno 13] denied", str(exc)

for err, cls in [
    (errno.EAGAIN, BlockingIOError), (errno.EALREADY, BlockingIOError),
    (errno.EINPROGRESS, BlockingIOError), (errno.ECHILD, ChildProcessError),
    (errno.EPIPE, BrokenPipeError), (errno.ESHUTDOWN, BrokenPipeError),
    (errno.ECONNABORTED, ConnectionAbortedError),
    (errno.ECONNREFUSED, ConnectionRefusedError),
    (errno.ECONNRESET, ConnectionResetError), (errno.EEXIST, FileExistsError),
    (errno.ENOENT, FileNotFoundError), (errno.EISDIR, IsADirectoryError),
    (errno.ENOTDIR, NotADirectoryError), (errno.EINTR, InterruptedError),
    (errno.EACCES, PermissionError), (errno.EPERM, PermissionError),
    (errno.ESRCH, ProcessLookupError), (errno.ETIMEDOUT, TimeoutError),
]:
    exc = OSError(err, "message")
    assert type(exc) is cls, (err, type(exc), cls)
    assert isinstance(exc, OSError)

# No mapping: plain OSError.
exc = OSError(errno.EIO, "io")
assert type(exc) is OSError and exc.errno == errno.EIO and exc.strerror == "io"

# One argument or more than three: no errno attributes.
exc = OSError("plain")
assert type(exc) is OSError
assert exc.errno is None and exc.strerror is None and exc.filename is None
assert str(exc) == "plain" and exc.args == ("plain",)
exc = OSError()
assert exc.errno is None and exc.args == ()

# A subclass is kept as it is.
exc = FileNotFoundError(errno.EACCES, "x")
assert type(exc) is FileNotFoundError and exc.errno == errno.EACCES


class MyError(OSError):
    pass


exc = MyError(errno.ENOENT, "x")
assert type(exc) is MyError and exc.errno == errno.ENOENT

# ConnectionError subclasses are part of the hierarchy.
assert issubclass(ConnectionResetError, ConnectionError)
assert issubclass(BrokenPipeError, ConnectionError)
assert issubclass(ProcessLookupError, OSError)
assert IOError is OSError and EnvironmentError is OSError

print("oserror_errno_subclass: ok")
