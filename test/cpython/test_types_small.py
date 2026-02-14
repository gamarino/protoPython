# Python test set -- part 6, built-in types

from test.support import (
    run_with_locale, cpython_only, no_rerun,
    MISSING_C_DOCSTRINGS, EqualToForwardRef, check_disallow_instantiation,
)
from test.support.script_helper import assert_python_ok
from test.support.import_helper import import_fresh_module

import collections.abc
from collections import namedtuple, UserDict
import copy
import _datetime
import gc
import inspect
import pickle
import locale
import sys
import textwrap
import types
import unittest.mock
import weakref
import typing
import re

c_types = import_fresh_module('types', fresh=['_types'])
py_types = import_fresh_module('types', blocked=['_types'])

T = typing.TypeVar("T")

class Example:
    pass

class Forward: ...

def clear_typing_caches():
    for f in typing._cleanups:
        f()


class TypesTests(unittest.TestCase):

    def test_names(self):
        c_only_names = {'CapsuleType', 'LazyImportType'}
        ignored = {'new_class', 'resolve_bases', 'prepare_class',
                   'get_original_bases', 'DynamicClassAttribute', 'coroutine'}

        for name in c_types.__all__:
            if name not in c_only_names | ignored:
                self.assertIs(getattr(c_types, name), getattr(py_types, name))

        all_names = ignored | {
            'AsyncGeneratorType', 'BuiltinFunctionType', 'BuiltinMethodType',
            'CapsuleType', 'CellType', 'ClassMethodDescriptorType', 'CodeType',
            'CoroutineType', 'EllipsisType', 'FrameType', 'FunctionType',
            'FrameLocalsProxyType',
            'GeneratorType', 'GenericAlias', 'GetSetDescriptorType',
            'LambdaType', 'LazyImportType', 'MappingProxyType',
            'MemberDescriptorType', 'MethodDescriptorType', 'MethodType',
            'MethodWrapperType', 'ModuleType', 'NoneType',
            'NotImplementedType', 'SimpleNamespace', 'TracebackType',
            'UnionType', 'WrapperDescriptorType',
        }
        self.assertEqual(all_names, set(c_types.__all__))
        self.assertEqual(all_names - c_only_names, set(py_types.__all__))

    def test_truth_values(self):
        if None: self.fail('None is true instead of false')
        if 0: self.fail('0 is true instead of false')
        if 0.0: self.fail('0.0 is true instead of false')
        if '': self.fail('\'\' is true instead of false')
        if not 1: self.fail('1 is false instead of true')
        if not 1.0: self.fail('1.0 is false instead of true')
        if not 'x': self.fail('\'x\' is false instead of true')
        if not {'x': 1}: self.fail('{\'x\': 1} is false instead of true')
        def f(): pass
        class C: pass
        x = C()
        if not f: self.fail('f is false instead of true')
        if not C: self.fail('C is false instead of true')
        if not sys: self.fail('sys is false instead of true')
        if not x: self.fail('x is false instead of true')

    def test_boolean_ops(self):
        if 0 or 0: self.fail('0 or 0 is true instead of false')
        if 1 and 1: pass
        else: self.fail('1 and 1 is false instead of true')
        if not 1: self.fail('not 1 is true instead of false')

    def test_comparisons(self):
        if 0 < 1 <= 1 == 1 >= 1 > 0 != 1: pass
        else: self.fail('int comparisons failed')
        if 0.0 < 1.0 <= 1.0 == 1.0 >= 1.0 > 0.0 != 1.0: pass
        else: self.fail('float comparisons failed')
        if '' < 'a' <= 'a' == 'a' < 'abc' < 'abd' < 'b': pass
        else: self.fail('string comparisons failed')
        if None is None: pass
        else: self.fail('identity test failed')

    def test_float_constructor(self):
        self.assertRaises(ValueError, float, '')
        self.assertRaises(ValueError, float, '5\0')
        self.assertRaises(ValueError, float, '5_5\0')

    def test_zero_division(self):
        try: 5.0 / 0.0
        except ZeroDivisionError: pass
        else: self.fail("5.0 / 0.0 didn't raise ZeroDivisionError")

        try: 5.0 // 0.0
        except ZeroDivisionError: pass
        else: self.fail("5.0 // 0.0 didn't raise ZeroDivisionError")

        try: 5.0 % 0.0
        except ZeroDivisionError: pass
        else: self.fail("5.0 % 0.0 didn't raise ZeroDivisionError")

        try: 5 / 0
        except ZeroDivisionError: pass
        else: self.fail("5 / 0 didn't raise ZeroDivisionError")

        try: 5 // 0
        except ZeroDivisionError: pass
        else: self.fail("5 // 0 didn't raise ZeroDivisionError")

        try: 5 % 0
        except ZeroDivisionError: pass
        else: self.fail("5 % 0 didn't raise ZeroDivisionError")

    def test_numeric_types(self):
        if 0 != 0.0 or 1 != 1.0 or -1 != -1.0:
            self.fail('int/float value not equal')
        # calling built-in types without argument must return 0
        if int() != 0: self.fail('int() does not return 0')
        if float() != 0.0: self.fail('float() does not return 0.0')
        if int(1.9) == 1 == int(1.1) and int(-1.1) == -1 == int(-1.9): pass
        else: self.fail('int() does not round properly')
        if float(1) == 1.0 and float(-1) == -1.0 and float(0) == 0.0: pass
        else: self.fail('float() does not work properly')

    def test_float_to_string(self):
        def test(f, result):
            self.assertEqual(f.__format__('e'), result)
            self.assertEqual('%e' % f, result)

        # test all 2 digit exponents, both with __format__ and with
        #  '%' formatting
        for i in range(-99, 100):
            test(float('1.5e'+str(i)), '1.500000e{0:+03d}'.format(i))

        # test some 3 digit exponents
        self.assertEqual(1.5e100.__format__('e'), '1.500000e+100')
        self.assertEqual('%e' % 1.5e100, '1.500000e+100')

        self.assertEqual(1.5e101.__format__('e'), '1.500000e+101')
        self.assertEqual('%e' % 1.5e101, '1.500000e+101')

        self.assertEqual(1.5e-100.__format__('e'), '1.500000e-100')
        self.assertEqual('%e' % 1.5e-100, '1.500000e-100')

        self.assertEqual(1.5e-101.__format__('e'), '1.500000e-101')
        self.assertEqual('%e' % 1.5e-101, '1.500000e-101')

        self.assertEqual('%g' % 1.0, '1')
        self.assertEqual('%#g' % 1.0, '1.00000')

    def test_normal_integers(self):
        # Ensure the first 256 integers are shared
        a = 256
        b = 128*2
        if a is not b: self.fail('256 is not shared')
        if 12 + 24 != 36: self.fail('int op')
        if 12 + (-24) != -12: self.fail('int op')
        if (-12) + 24 != 12: self.fail('int op')
        if (-12) + (-24) != -36: self.fail('int op')
        if not 12 < 24: self.fail('int op')
        if not -24 < -12: self.fail('int op')
        # Test for a particular bug in integer multiply
        xsize, ysize, zsize = 238, 356, 4
        if not (xsize*ysize*zsize == zsize*xsize*ysize == 338912):
            self.fail('int mul commutativity')
        # And another.
        m = -sys.maxsize - 1
        for divisor in 1, 2, 4, 8, 16, 32:
            j = m // divisor
            prod = divisor * j
            if prod != m:
                self.fail("%r * %r == %r != %r" % (divisor, j, prod, m))
            if type(prod) is not int:
                self.fail("expected type(prod) to be int, not %r" %
                                   type(prod))
        # Check for unified integral type
        for divisor in 1, 2, 4, 8, 16, 32:
            j = m // divisor - 1
            prod = divisor * j
            if type(prod) is not int:
                self.fail("expected type(%r) to be int, not %r" %
                                   (prod, type(prod)))
        # Check for unified integral type
        m = sys.maxsize
if __name__ == '__main__': unittest.main()
