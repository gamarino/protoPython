# Struct sequences (sys.version_info, sys.flags, sys.*_info, os.stat_result,
# time.struct_time) behave as tuples of their visible fields and expose every
# named field as an attribute. Field names and order follow CPython 3.14; the
# script also passes under CPython, except that the repr check is limited to
# protoPython, whose struct sequences print as plain tuples.
import os
import sys
import time

PROTOPYTHON = sys.implementation.name == "protopython"


def check(label, obj, fields, n_visible):
    assert isinstance(obj, tuple), label
    t = tuple(obj)  # used to loop forever for os.stat_result
    assert len(obj) == n_visible and len(t) == n_visible, (label, len(obj), len(t))
    assert list(obj) == list(t), label
    for i, name in enumerate(fields[:n_visible]):
        if name is not None:
            assert obj[i] == getattr(obj, name), (label, i, name)
    for name in fields[n_visible:]:
        assert hasattr(obj, name), (label, name)
    assert obj[0] == t[0] and obj[-1] == t[-1], label
    assert obj[:2] == t[:2] and obj[1:] == t[1:] and obj[::-1] == t[::-1], label
    assert type(obj[:2]) is tuple, label
    assert obj == t and t == obj and not (obj != t), label
    assert obj >= t and obj <= t and obj < t + (0,), label
    assert hash(obj) == hash(t), label
    first, *rest = obj
    assert first == t[0] and len(rest) == n_visible - 1, label
    try:
        obj[n_visible]
    except IndexError:
        pass
    else:
        raise AssertionError(label + ": index past the end must raise IndexError")
    if PROTOPYTHON:
        assert repr(obj) == repr(t), (label, repr(obj))


VERSION_FIELDS = ["major", "minor", "micro", "releaselevel", "serial"]

vi = sys.version_info
check("sys.version_info", vi, VERSION_FIELDS, 5)
assert vi[:2] == (3, 14) and vi.major == 3 and vi.minor == 14
assert vi >= (3, 8) and vi < (4,) and not (vi < (3, 14))
assert vi.releaselevel in ("alpha", "beta", "candidate", "final")
assert "%d.%d" % vi[:2] == "3.14"

check("sys.implementation.version", sys.implementation.version, VERSION_FIELDS, 5)

check("sys.flags", sys.flags, [
    "debug", "inspect", "interactive", "optimize", "dont_write_bytecode",
    "no_user_site", "no_site", "ignore_environment", "verbose", "bytes_warning",
    "quiet", "hash_randomization", "isolated", "dev_mode", "utf8_mode",
    "warn_default_encoding", "safe_path", "int_max_str_digits",
    "context_aware_warnings", "thread_inherit_context"], 18)
assert sys.flags[3] == sys.flags.optimize
assert isinstance(sys.flags.dev_mode, bool) and isinstance(sys.flags.safe_path, bool)

check("sys.hash_info", sys.hash_info, [
    "width", "modulus", "inf", "nan", "imag", "algorithm", "hash_bits",
    "seed_bits", "cutoff"], 9)
check("sys.int_info", sys.int_info, [
    "bits_per_digit", "sizeof_digit", "default_max_str_digits",
    "str_digits_check_threshold"], 4)
check("sys.thread_info", sys.thread_info, ["name", "lock", "version"], 3)
check("sys.float_info", sys.float_info, [
    "max", "max_exp", "max_10_exp", "min", "min_exp", "min_10_exp", "dig",
    "mant_dig", "epsilon", "radix", "rounds"], 11)
assert sys.float_info[0] == sys.float_info.max and sys.float_info.radix == 2

STAT_FIELDS = [
    "st_mode", "st_ino", "st_dev", "st_nlink", "st_uid", "st_gid", "st_size",
    None, None, None,
    "st_atime", "st_mtime", "st_ctime", "st_atime_ns", "st_mtime_ns",
    "st_ctime_ns", "st_blksize", "st_blocks", "st_rdev"]

here = os.path.dirname(os.path.abspath(__file__))
for label, st in (("os.stat", os.stat(here)), ("os.lstat", os.lstat(here))):
    check(label, st, STAT_FIELDS, 10)
    assert st[:3] == (st.st_mode, st.st_ino, st.st_dev), label
    assert tuple(st)[6] == st.st_size, label
    assert isinstance(st[7], int) and st[7] == int(st.st_atime), label
    assert isinstance(st.st_mtime, float) and st[8] == int(st.st_mtime), label
    assert st.st_mtime_ns // 1000000000 == st[8], label

fd = os.open(os.path.abspath(__file__), os.O_RDONLY)
try:
    st = os.fstat(fd)
finally:
    os.close(fd)
check("os.fstat", st, STAT_FIELDS, 10)
assert st.st_size == os.stat(os.path.abspath(__file__)).st_size

TIME_FIELDS = ["tm_year", "tm_mon", "tm_mday", "tm_hour", "tm_min", "tm_sec",
               "tm_wday", "tm_yday", "tm_isdst"]
for label, tm in (("time.localtime", time.localtime()), ("time.gmtime", time.gmtime(0))):
    check(label, tm, TIME_FIELDS, 9)
assert time.gmtime(0)[:3] == (1970, 1, 1)

print("OK")
