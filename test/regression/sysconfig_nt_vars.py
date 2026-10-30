# sysconfig's Windows initialisation imports _sysconfig, which CPython builds
# in; without it `import test.support` failed on Windows.
import _sysconfig
import sysconfig  # noqa: F401  (imports cleanly)

v = _sysconfig.config_vars()
assert v['Py_DEBUG'] == 0 and v['Py_GIL_DISABLED'] == 0 and 'EXT_SUFFIX' in v
import test.support  # noqa: E402,F401
print("sysconfig_nt_vars: ok")
