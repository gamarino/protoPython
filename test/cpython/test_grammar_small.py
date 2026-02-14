# Python test set -- part 1, grammar.
# This just tests whether the parser accepts them all.

import annotationlib
import inspect
import unittest
import sys
import textwrap
import warnings
# testing import *
from sys import *

# different import patterns to check that __annotations__ does not interfere
# with import machinery
import test.typinganndata.ann_module as ann_module
import typing
from test.typinganndata import ann_module2
import test
from test.support import (
    check_syntax_error,
    import_helper,
    skip_emscripten_stack_overflow,
    skip_wasi_stack_overflow,
)
from test.support.numbers import (
    VALID_UNDERSCORE_LITERALS,
    INVALID_UNDERSCORE_LITERALS,
)

class TokenTests(unittest.TestCase):

    from test.support import check_syntax_error
    from test.support.warnings_helper import check_syntax_warning

    def test_backslash(self):
        # Backslash means line continuation:
        x = 1 \
        + 1
        self.assertEqual(x, 2, 'backslash for line continuation')

        # Backslash does not means continuation in comments :\
        x = 0
        self.assertEqual(x, 0, 'backslash ending comment')

    def test_plain_integers(self):
        self.assertEqual(type(000), type(0))
        self.assertEqual(0xff, 255)
        self.assertEqual(0o377, 255)
        self.assertEqual(2147483647, 0o17777777777)
        self.assertEqual(0b1001, 9)
        # "0x" is not a valid literal
        self.assertRaises(SyntaxError, eval, "0x")
        from sys import maxsize
        if maxsize == 2147483647:
            self.assertEqual(-2147483647-1, -0o20000000000)
            # XXX -2147483648
            self.assertTrue(0o37777777777 > 0)
            self.assertTrue(0xffffffff > 0)
            self.assertTrue(0b1111111111111111111111111111111 > 0)
            for s in ('2147483648', '0o40000000000', '0x100000000',
                      '0b10000000000000000000000000000000'):
                try:
                    x = eval(s)
                except OverflowError:
                    self.fail("OverflowError on huge integer literal %r" % s)
        elif maxsize == 9223372036854775807:
            self.assertEqual(-9223372036854775807-1, -0o1000000000000000000000)
            self.assertTrue(0o1777777777777777777777 > 0)
            self.assertTrue(0xffffffffffffffff > 0)
            self.assertTrue(0b11111111111111111111111111111111111111111111111111111111111111 > 0)
            for s in '9223372036854775808', '0o2000000000000000000000', \
                     '0x10000000000000000', \
                     '0b100000000000000000000000000000000000000000000000000000000000000':
                try:
                    x = eval(s)
                except OverflowError:
                    self.fail("OverflowError on huge integer literal %r" % s)
        else:
            self.fail('Weird maxsize value %r' % maxsize)

    def test_long_integers(self):
        x = 0
        x = 0xffffffffffffffff
        x = 0Xffffffffffffffff
        x = 0o77777777777777777
        x = 0O77777777777777777
        x = 123456789012345678901234567890
        x = 0b100000000000000000000000000000000000000000000000000000000000000000000
        x = 0B111111111111111111111111111111111111111111111111111111111111111111111

    def test_floats(self):
        x = 3.14
        x = 314.
        x = 0.314
        x = 000.314
        x = .314
        x = 3e14
        x = 3E14
        x = 3e-14
        x = 3e+14
        x = 3.e14
        x = .3e14
        x = 3.1e4

    def test_float_exponent_tokenization(self):
        # See issue 21642.
        with warnings.catch_warnings():
            warnings.simplefilter('ignore', SyntaxWarning)
            self.assertEqual(eval("1 if 1else 0"), 1)
            self.assertEqual(eval("1 if 0else 0"), 0)
        self.assertRaises(SyntaxError, eval, "0 if 1Else 0")

    def test_underscore_literals(self):
        for lit in VALID_UNDERSCORE_LITERALS:
            self.assertEqual(eval(lit), eval(lit.replace('_', '')))
        for lit in INVALID_UNDERSCORE_LITERALS:
            self.assertRaises(SyntaxError, eval, lit)
        # Sanity check: no literal begins with an underscore
        self.assertRaises(NameError, eval, "_0")

    def test_bad_numerical_literals(self):
        check = self.check_syntax_error
        check("0b12", "invalid digit '2' in binary literal")
        check("0b1_2", "invalid digit '2' in binary literal")
        check("0b2", "invalid digit '2' in binary literal")
        check("0b1_", "invalid binary literal")
        check("0b", "invalid binary literal")
        check("0o18", "invalid digit '8' in octal literal")
        check("0o1_8", "invalid digit '8' in octal literal")
        check("0o8", "invalid digit '8' in octal literal")
        check("0o1_", "invalid octal literal")
        check("0o", "invalid octal literal")
        check("0x1_", "invalid hexadecimal literal")
        check("0x", "invalid hexadecimal literal")
        check("1_", "invalid decimal literal")
        check("012",
              "leading zeros in decimal integer literals are not permitted; "
              "use an 0o prefix for octal integers")
        check("1.2_", "invalid decimal literal")
        check("1e2_", "invalid decimal literal")
        check("1e+", "invalid decimal literal")

    def test_end_of_numerical_literals(self):
        def check(test, error=False):
            with self.subTest(expr=test):
                if error:
                    with warnings.catch_warnings(record=True) as w:
                        with self.assertRaisesRegex(SyntaxError,
                                    r'invalid \w+ literal'):
                            compile(test, "<testcase>", "eval")
                    self.assertEqual(w,  [])
                else:
                    self.check_syntax_warning(test,
                            errtext=r'invalid \w+ literal')

        for num in "0xf", "0o7", "0b1", "9", "0", "1.", "1e3", "1j":
            compile(num, "<testcase>", "eval")
            check(f"{num}and x", error=(num == "0xf"))
            check(f"{num}or x", error=(num == "0"))
            check(f"{num}in x")
            check(f"{num}not in x")
            check(f"{num}if x else y")
            check(f"x if {num}else y", error=(num == "0xf"))
            check(f"[{num}for x in ()]")
            check(f"{num}spam", error=True)

            # gh-88943: Invalid non-ASCII character following a numerical literal.
            with self.assertRaisesRegex(SyntaxError, r"invalid character '⁄' \(U\+2044\)"):
                compile(f"{num}⁄7", "<testcase>", "eval")

            with self.assertWarnsRegex(SyntaxWarning, r'invalid \w+ literal'):
                compile(f"{num}is x", "<testcase>", "eval")
            with warnings.catch_warnings():
                warnings.simplefilter('error', SyntaxWarning)
                with self.assertRaisesRegex(SyntaxError,
                            r'invalid \w+ literal'):
                    compile(f"{num}is x", "<testcase>", "eval")

        check("[0x1ffor x in ()]")
        check("[0x1for x in ()]")
        check("[0xfor x in ()]")

    def test_string_literals(self):
        x = ''; y = ""; self.assertTrue(len(x) == 0 and x == y)
        x = '\''; y = "'"; self.assertTrue(len(x) == 1 and x == y and ord(x) == 39)
        x = '"'; y = "\""; self.assertTrue(len(x) == 1 and x == y and ord(x) == 34)
        x = "doesn't \"shrink\" does it"
        y = 'doesn\'t "shrink" does it'
        self.assertTrue(len(x) == 24 and x == y)
        x = "does \"shrink\" doesn't it"
        y = 'does "shrink" doesn\'t it'
        self.assertTrue(len(x) == 24 and x == y)
        x = """
The "quick"
brown fox
jumps over
the 'lazy' dog.
"""
        y = '\nThe "quick"\nbrown fox\njumps over\nthe \'lazy\' dog.\n'
        self.assertEqual(x, y)
