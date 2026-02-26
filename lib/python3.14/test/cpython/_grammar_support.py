"""
Minimal test-support replacements for test_grammar.py when run under protoPython.
Avoids importing test.support (and thus annotationlib, contextlib, etc.) so the
grammar test can complete within the conformance timeout.
"""

import unittest
import warnings


class _CheckNoWarnings:
    """Context manager: assert no warnings are emitted in the block."""
    def __init__(self, testcase, message='', category=Warning, force_gc=False):
        self.testcase = testcase
        self.message = message
        self.category = category
        self.force_gc = force_gc

    def __enter__(self):
        self._ctx = warnings.catch_warnings(record=True)
        self._warns = self._ctx.__enter__()
        warnings.simplefilter('always', self.category)
        return self

    def __exit__(self, *exc):
        if self.force_gc:
            try:
                import gc
                gc.collect()
            except Exception:
                pass
        self._ctx.__exit__(*exc)
        self.testcase.assertEqual(self._warns, [])
        return False


def check_no_warnings(testcase, message='', category=Warning, force_gc=False):
    return _CheckNoWarnings(testcase, message=message, category=category, force_gc=force_gc)

# Same literal lists as test.support.numbers (used by test_underscore_literals).
VALID_UNDERSCORE_LITERALS = [
    '0_0_0',
    '4_2',
    '1_0000_0000',
    '0b1001_0100',
    '0xffff_ffff',
    '0o5_7_7',
    '1_00_00.5',
    '1_00_00.5e5',
    '1_00_00e5_1',
    '1e1_0',
    '.1_4',
    '.1_4e1',
    '0b_0',
    '0x_f',
    '0o_5',
    '1_00_00j',
    '1_00_00.5j',
    '1_00_00e5_1j',
    '.1_4j',
    '(1_2.5+3_3j)',
    '(.5_6j)',
]
INVALID_UNDERSCORE_LITERALS = [
    '0_',
    '42_',
    '1.4j_',
    '0x_',
    '0b1_',
    '0xf_',
    '0o5_',
    '0 if 1_Else 1',
    '0_b0',
    '0_xf',
    '0_o5',
    '0_7',
    '09_99',
    '4_______2',
    '0.1__4',
    '0.1__4j',
    '0b1001__0100',
    '0xffff__ffff',
    '0x___',
    '0o5__77',
    '1e1__0',
    '1e1__0j',
    '1_.4',
    '1_.4j',
    '1._4',
    '1._4j',
    '._5',
    '._5j',
    '1.0e+_1',
    '1.0e+_1j',
    '1.4_j',
    '1.4e5_j',
    '1_e1',
    '1.4_e1',
    '1.4_e1j',
    '1e_1',
    '1.4e_1',
    '1.4e_1j',
    '(1+1.5_j_)',
    '(1+1.5_j)',
]


def check_syntax_error(testcase, statement, errtext='', *, lineno=None, offset=None):
    """Assert that compiling statement raises SyntaxError matching errtext."""
    with testcase.assertRaisesRegex(SyntaxError, errtext) as cm:
        compile(statement, '<test string>', 'exec')
    err = cm.exception
    testcase.assertIsNotNone(err.lineno)
    if lineno is not None:
        testcase.assertEqual(err.lineno, lineno)
    testcase.assertIsNotNone(err.offset)
    if offset is not None:
        testcase.assertEqual(err.offset, offset)


def check_syntax_warning(testcase, statement, errtext='', *, lineno=1, offset=None):
    """Assert that compiling statement emits exactly one SyntaxWarning matching errtext."""
    with warnings.catch_warnings(record=True) as warns:
        warnings.simplefilter('always', SyntaxWarning)
        compile(statement, '<testcase>', 'exec')
    testcase.assertEqual(len(warns), 1, warns)
    warn, = warns
    testcase.assertIsSubclass(warn.category, SyntaxWarning)
    if errtext:
        testcase.assertRegex(str(warn.message), errtext)
    testcase.assertEqual(warn.filename, '<testcase>')
    testcase.assertIsNotNone(warn.lineno)
    if lineno is not None:
        testcase.assertEqual(warn.lineno, lineno)
    with warnings.catch_warnings(record=True) as w2:
        warnings.simplefilter('error', SyntaxWarning)
        with testcase.assertRaisesRegex(SyntaxError, errtext):
            compile(statement, '<testcase>', 'exec')
    testcase.assertEqual(w2, [])


def skip_emscripten_stack_overflow():
    return unittest.skipIf(False, "Exhausts stack on Emscripten")


def skip_wasi_stack_overflow():
    return unittest.skipIf(False, "Exhausts stack on WASI")


class _ImportHelper:
    """Minimal import_helper for test_var_annot_in_module (single use)."""
    @staticmethod
    def import_fresh_module(name):
        import importlib
        return importlib.import_module(name)


import_helper = _ImportHelper()
