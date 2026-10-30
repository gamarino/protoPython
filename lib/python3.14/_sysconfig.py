"""Build configuration variables that sysconfig reads on Windows.

CPython builds this module in (Modules/_sysconfig.c); sysconfig's
_init_non_posix imports it, so on Windows `sysconfig.get_config_vars()`,
and with it `import test.support`, failed with ModuleNotFoundError.
"""


def config_vars():
    """The variables CPython's _sysconfig.config_vars() reports.

    protoPython loads extension modules through HPy, not CPython's ABI, so
    the suffix is the untagged '.pyd' and there is no SOABI; the values
    match the POSIX configuration data (no debug build, Py_GIL_DISABLED 0).
    """
    return {
        'EXT_SUFFIX': '.pyd',
        'SOABI': None,
        'Py_GIL_DISABLED': 0,
        'Py_DEBUG': 0,
    }
