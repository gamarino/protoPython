# sysconfig reads its build-time variables from the _sysconfigdata module
# named after sys.implementation._multiarch.  Without _multiarch protoPython
# found no data and every variable (Py_DEBUG, Py_GIL_DISABLED) was None.
import os
import sys
import sysconfig

if sys.platform == "linux" and os.path.exists(os.path.join(
        os.path.dirname(sysconfig.__file__), "..",
        sysconfig._get_sysconfigdata_name() + ".py")) or sys.implementation.name == "cpython":
    assert sysconfig.get_config_var("Py_DEBUG") == 0, sysconfig.get_config_var("Py_DEBUG")
    assert sysconfig.get_config_var("Py_GIL_DISABLED") in (0, 1)
if sys.platform == "linux":
    assert sys.implementation._multiarch.endswith("-linux-gnu") or \
        sys.implementation._multiarch.startswith("arm"), sys.implementation._multiarch
print("sysconfig_build_vars: ok")
