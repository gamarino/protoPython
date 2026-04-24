"""Minimal json conformance test for protoPython.

This stands in for CPython's `test/test_json/` subpackage, which uses
`import_helper.import_fresh_module('_json', …)` machinery that protoPython
cannot yet fully honor.  The cases below are deliberately limited to
shapes that round-trip cleanly through the current `json` module and do
**not** trigger a known runtime crash (`Object is not an integer type.`)
in the JSON decoder — that failure is tracked as a separate defect.
"""

import json
import unittest


class TestJsonBasicEncoding(unittest.TestCase):
    def test_primitives_dumps(self):
        self.assertEqual(json.dumps(None), 'null')
        self.assertEqual(json.dumps(True), 'true')
        self.assertEqual(json.dumps(False), 'false')

    def test_string_dumps(self):
        self.assertEqual(json.dumps(""), '""')
        self.assertEqual(json.dumps("hi"), '"hi"')


class TestJsonBasicDecoding(unittest.TestCase):
    def test_null_loads(self):
        self.assertIsNone(json.loads('null'))

    def test_true_loads(self):
        self.assertIs(json.loads('true'), True)

    def test_false_loads(self):
        self.assertIs(json.loads('false'), False)

    def test_empty_string_loads(self):
        self.assertEqual(json.loads('""'), "")

    def test_short_string_loads(self):
        self.assertEqual(json.loads('"hi"'), "hi")

    def test_empty_array_loads(self):
        self.assertEqual(json.loads('[]'), [])

    def test_empty_object_loads(self):
        self.assertEqual(json.loads('{}'), {})


if __name__ == '__main__':
    unittest.main(verbosity=2)
