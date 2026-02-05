# unittest.py - Minimal TestCase with assertEqual, assertTrue, assertFalse, fail, assertRaises, assertIn.

class TestCase:
    """Minimal TestCase with basic assertions."""

    def assertEqual(self, first, second, msg=None):
        if first != second:
            raise AssertionError(msg or "%r != %r" % (first, second))

    def assertTrue(self, expr, msg=None):
        if not expr:
            raise AssertionError(msg or "False is not true")

    def assertFalse(self, expr, msg=None):
        if expr:
            raise AssertionError(msg or "%r is not false" % expr)

    def assertIn(self, member, container, msg=None):
        if member not in container:
            raise AssertionError(msg or "%r not found in %r" % (member, container))

    def assertRaises(self, exc_type, callable_obj=None, *args, **kwargs):
        if callable_obj is None:
            return _AssertRaisesContext(exc_type, self)
        try:
            callable_obj(*args, **kwargs)
        except exc_type:
            return
        raise AssertionError("%s not raised" % exc_type.__name__)

    def fail(self, msg=None):
        raise AssertionError(msg or "failure")

    def run(self):
        """Run test methods (names starting with test_)."""
        for name in dir(self):
            if name.startswith("test_"):
                m = getattr(self, name)
                if callable(m):
                    m()

class _AssertRaisesContext:
    def __init__(self, expected, test_case):
        self.expected = expected
        self.test_case = test_case

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if exc_type is None:
            raise AssertionError("%s not raised" % self.expected.__name__)
        if not issubclass(exc_type, self.expected):
            return False
        return True

def main(module=None, **kwargs):
    """Minimal unittest.main: discover and run tests from __main__."""
    if module is None:
        import sys
        module = sys.modules.get("__main__")
    if module is None:
        return
    for name in dir(module):
        obj = getattr(module, name)
        if isinstance(obj, type) and issubclass(obj, TestCase) and obj is not TestCase:
            inst = obj()
            for mname in dir(inst):
                if mname.startswith("test_"):
                    m = getattr(inst, mname)
                    if callable(m):
                        m()
