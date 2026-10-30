# test.support's helper modules import on every platform.  On Windows
# os_helper reads sys.getwindowsversion(), which protoPython lacked, so
# import_helper (and every test that uses it) failed there.
import sys

from test.support import import_helper, os_helper  # noqa: F401

if sys.platform == "win32":
    v = sys.getwindowsversion()
    assert v.major >= 6 and v.platform == 2, v
    assert v[:3] == (v.major, v.minor, v.build) and len(v) == 5
    assert isinstance(v.service_pack, str) and v.product_type in (1, 2, 3)
    assert len(v.platform_version) == 3
else:
    assert not hasattr(sys, "getwindowsversion")
print("test_support_helpers_import: ok")
