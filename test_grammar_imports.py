import time

def test_import(stmt):
    print(f"Executing: {stmt}")
    t0 = time.time()
    try:
        exec(stmt)
        print(f"  Done in {time.time() - t0:.2f}s")
    except Exception as e:
        print(f"  Failed: {repr(e)} in {time.time() - t0:.2f}s")

test_import("from test.support import check_syntax_error, skip_wasi_stack_overflow")
test_import("from test.support import import_helper")
test_import("import annotationlib")
test_import("import inspect")
test_import("import unittest")
test_import("import sys")
test_import("import textwrap")
test_import("import warnings")
test_import("from sys import *")
test_import("import test.typinganndata.ann_module as ann_module")
test_import("import typing")
test_import("from test.typinganndata import ann_module2")
test_import("import test")
test_import("from test.support.numbers import VALID_UNDERSCORE_LITERALS, INVALID_UNDERSCORE_LITERALS")
