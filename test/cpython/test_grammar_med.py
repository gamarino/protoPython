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
        y = '''
The "quick"
brown fox
jumps over
the 'lazy' dog.
'''
        self.assertEqual(x, y)
        y = "\n\
The \"quick\"\n\
brown fox\n\
jumps over\n\
the 'lazy' dog.\n\
"
        self.assertEqual(x, y)
        y = '\n\
The \"quick\"\n\
brown fox\n\
jumps over\n\
the \'lazy\' dog.\n\
'
        self.assertEqual(x, y)

    def test_string_prefixes(self):
        def check(s):
            parsed = eval(s)
            self.assertIs(type(parsed), str)
            self.assertGreater(len(parsed), 0)

        check("u'abc'")
        check("r'abc\t'")
        check("rf'abc\a {1 + 1}'")
        check("fr'abc\a {1 + 1}'")

    def test_bytes_prefixes(self):
        def check(s):
            parsed = eval(s)
            self.assertIs(type(parsed), bytes)
            self.assertGreater(len(parsed), 0)

        check("b'abc'")
        check("br'abc\t'")
        check("rb'abc\a'")

    def test_ellipsis(self):
        x = ...
        self.assertTrue(x is Ellipsis)
        self.assertRaises(SyntaxError, eval, ".. .")

    def test_eof_error(self):
        samples = ("def foo(", "\ndef foo(", "def foo(\n")
        for s in samples:
            with self.assertRaises(SyntaxError) as cm:
                compile(s, "<test>", "exec")
            self.assertIn("was never closed", str(cm.exception))

    @skip_wasi_stack_overflow()
    @skip_emscripten_stack_overflow()
    def test_max_level(self):
        # Macro defined in Parser/lexer/state.h
        MAXLEVEL = 200

        result = eval("(" * MAXLEVEL + ")" * MAXLEVEL)
        self.assertEqual(result, ())

        with self.assertRaises(SyntaxError) as cm:
            eval("(" * (MAXLEVEL + 1) + ")" * (MAXLEVEL + 1))
        self.assertStartsWith(str(cm.exception), 'too many nested parentheses')

var_annot_global: int # a global annotated is necessary for test_var_annot


class GrammarTests(unittest.TestCase):

    from test.support import check_syntax_error
    from test.support.warnings_helper import check_syntax_warning
    from test.support.warnings_helper import check_no_warnings

    # single_input: NEWLINE | simple_stmt | compound_stmt NEWLINE
    # XXX can't test in a script -- this rule is only used when interactive

    # file_input: (NEWLINE | stmt)* ENDMARKER
    # Being tested as this very moment this very module

    # expr_input: testlist NEWLINE
    # XXX Hard to test -- used only in calls to input()

    def test_eval_input(self):
        # testlist ENDMARKER
        x = eval('1, 0 or 1')

    def test_var_annot_basics(self):
        # all these should be allowed
        var1: int = 5
        var2: [int, str]
        my_lst = [42]
        def one():
            return 1
        int.new_attr: int
        [list][0]: type
        my_lst[one()-1]: int = 5
        self.assertEqual(my_lst, [5])

    def test_var_annot_syntax_errors(self):
        # parser pass
        check_syntax_error(self, "def f: int")
        check_syntax_error(self, "x: int: str")
        check_syntax_error(self, "def f():\n"
                                 "    nonlocal x: int\n")
        check_syntax_error(self, "def f():\n"
                                 "    global x: int\n")
        check_syntax_error(self, "x: int = y = 1")
        check_syntax_error(self, "z = w: int = 1")
        check_syntax_error(self, "x: int = y: int = 1")
        # AST pass
        check_syntax_error(self, "[x, 0]: int\n")
        check_syntax_error(self, "f(): int\n")
        check_syntax_error(self, "(x,): int")
        check_syntax_error(self, "def f():\n"
                                 "    (x, y): int = (1, 2)\n")
        # symtable pass
        check_syntax_error(self, "def f():\n"
                                 "    x: int\n"
                                 "    global x\n")
        check_syntax_error(self, "def f():\n"
                                 "    global x\n"
                                 "    x: int\n")
        check_syntax_error(self, "def f():\n"
                                 "    x: int\n"
                                 "    nonlocal x\n")
        check_syntax_error(self, "def f():\n"
                                 "    nonlocal x\n"
                                 "    x: int\n")

    def test_var_annot_basic_semantics(self):
        # execution order
        with self.assertRaises(ZeroDivisionError):
            no_name[does_not_exist]: no_name_again = 1/0
        with self.assertRaises(NameError):
            no_name[does_not_exist]: 1/0 = 0
        global var_annot_global

        # function semantics
        def f():
            st: str = "Hello"
            a.b: int = (1, 2)
            return st
        self.assertEqual(f.__annotations__, {})
        def f_OK():
            x: 1/0
        f_OK()
        def fbad():
            x: int
            print(x)
        with self.assertRaises(UnboundLocalError):
            fbad()
        def f2bad():
            (no_such_global): int
            print(no_such_global)
        try:
            f2bad()
        except Exception as e:
            self.assertIs(type(e), NameError)

        # class semantics
        class C:
            __foo: int
            s: str = "attr"
            z = 2
            def __init__(self, x):
                self.x: int = x
        self.assertEqual(C.__annotations__, {'_C__foo': int, 's': str})
        with self.assertRaises(NameError):
            class CBad:
                no_such_name_defined.attr: int = 0
        with self.assertRaises(NameError):
            class Cbad2(C):
                x: int
                x.y: list = []

    def test_annotations_inheritance(self):
        # Check that annotations are not inherited by derived classes
        class A:
            attr: int
        class B(A):
            pass
        class C(A):
            attr: str
        class D:
            attr2: int
        class E(A, D):
            pass
        class F(C, A):
            pass
        self.assertEqual(A.__annotations__, {"attr": int})
        self.assertEqual(B.__annotations__, {})
        self.assertEqual(C.__annotations__, {"attr" : str})
        self.assertEqual(D.__annotations__, {"attr2" : int})
        self.assertEqual(E.__annotations__, {})
        self.assertEqual(F.__annotations__, {})

    def test_var_annot_module_semantics(self):
        self.assertEqual(test.__annotations__, {})
        self.assertEqual(ann_module.__annotations__,
                         {'x': int, 'y': str, 'f': typing.Tuple[int, int], 'u': int | float})
        self.assertEqual(ann_module.M.__annotations__,
                         {'o': type})
        self.assertEqual(ann_module2.__annotations__, {})

    def test_var_annot_in_module(self):
        # check that functions fail the same way when executed
        # outside of module where they were defined
        ann_module3 = import_helper.import_fresh_module("test.typinganndata.ann_module3")
        with self.assertRaises(NameError):
            ann_module3.f_bad_ann()
        with self.assertRaises(NameError):
            ann_module3.g_bad_ann()
        with self.assertRaises(NameError):
            ann_module3.D_bad_ann(5)

    def test_var_annot_simple_exec(self):
        gns = {}; lns = {}
        exec("'docstring'\n"
             "x: int = 5\n", gns, lns)
        self.assertNotIn('__annotate__', gns)

        gns.update(lns)  # __annotate__ looks at globals
        self.assertEqual(lns["__annotate__"](annotationlib.Format.VALUE), {'x': int})

    def test_var_annot_rhs(self):
        ns = {}
        exec('x: tuple = 1, 2', ns)
        self.assertEqual(ns['x'], (1, 2))
        stmt = ('def f():\n'
                '    x: int = yield')
        exec(stmt, ns)
        self.assertEqual(list(ns['f']()), [None])

        ns = {"a": 1, 'b': (2, 3, 4), "c":5, "Tuple": typing.Tuple}
        exec('x: Tuple[int, ...] = a,*b,c', ns)
        self.assertEqual(ns['x'], (1, 2, 3, 4, 5))

    def test_funcdef(self):
        ### [decorators] 'def' NAME parameters ['->' test] ':' suite
        ### decorator: '@' namedexpr_test NEWLINE
        ### decorators: decorator+
        ### parameters: '(' [typedargslist] ')'
        ### typedargslist: ((tfpdef ['=' test] ',')*
        ###                ('*' [tfpdef] (',' tfpdef ['=' test])* [',' '**' tfpdef] | '**' tfpdef)
        ###                | tfpdef ['=' test] (',' tfpdef ['=' test])* [','])
        ### tfpdef: NAME [':' test]
        ### varargslist: ((vfpdef ['=' test] ',')*
        ###              ('*' [vfpdef] (',' vfpdef ['=' test])*  [',' '**' vfpdef] | '**' vfpdef)
        ###              | vfpdef ['=' test] (',' vfpdef ['=' test])* [','])
        ### vfpdef: NAME
        def f1(): pass
        f1()
        f1(*())
        f1(*(), **{})
        def f2(one_argument): pass
        def f3(two, arguments): pass
        self.assertEqual(f2.__code__.co_varnames, ('one_argument',))
        self.assertEqual(f3.__code__.co_varnames, ('two', 'arguments'))
        def a1(one_arg,): pass
        def a2(two, args,): pass
        def v0(*rest): pass
        def v1(a, *rest): pass
        def v2(a, b, *rest): pass

        f1()
        f2(1)
        f2(1,)
        f3(1, 2)
        f3(1, 2,)
        v0()
        v0(1)
        v0(1,)
        v0(1,2)
        v0(1,2,3,4,5,6,7,8,9,0)
        v1(1)
        v1(1,)
        v1(1,2)
        v1(1,2,3)
        v1(1,2,3,4,5,6,7,8,9,0)
        v2(1,2)
        v2(1,2,3)
        v2(1,2,3,4)
        v2(1,2,3,4,5,6,7,8,9,0)

        def d01(a=1): pass
        d01()
        d01(1)
        d01(*(1,))
        d01(*[] or [2])
        d01(*() or (), *{} and (), **() or {})
        d01(**{'a':2})
        d01(**{'a':2} or {})
        def d11(a, b=1): pass
        d11(1)
        d11(1, 2)
        d11(1, **{'b':2})
