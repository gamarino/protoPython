#!/usr/bin/env python3
"""
Self-test for run_conformity.py, run with the host Python interpreter.

Checks that the runner finds protopy where the build places it
(build*/src/runtime/protopy, then PATH) and that it passes the import
directory to protopy through PROTO_PYTHONPATH, the variable protopy reads.
"""
import os
import stat
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_conformity  # noqa: E402


def make_executable(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write("#!/bin/sh\nexit 0\n")
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)


class BinaryDiscoveryTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = self._tmp.name
        self.empty_path = os.path.join(self.root, "no-bin")
        os.makedirs(self.empty_path)

    def tearDown(self):
        self._tmp.cleanup()

    def discover(self, environ):
        return run_conformity.get_proto_python(environ=environ, repo_root=self.root)

    def test_explicit_variable_wins(self):
        make_executable(os.path.join(self.root, "build_release", "src", "runtime", "protopy"))
        self.assertEqual(self.discover({"PROTO_PYTHON": "/x/protopy", "PATH": self.empty_path}),
                         "/x/protopy")

    def test_build_release_before_build(self):
        release = os.path.join(self.root, "build_release", "src", "runtime", "protopy")
        make_executable(release)
        make_executable(os.path.join(self.root, "build", "src", "runtime", "protopy"))
        self.assertEqual(self.discover({"PATH": self.empty_path}), release)

    def test_build_directory(self):
        debug = os.path.join(self.root, "build", "src", "runtime", "protopy")
        make_executable(debug)
        self.assertEqual(self.discover({"PATH": self.empty_path}), debug)

    def test_path_lookup(self):
        on_path = os.path.join(self.root, "bin", "protopy")
        make_executable(on_path)
        self.assertEqual(self.discover({"PATH": os.path.dirname(on_path)}), on_path)

    def test_not_found(self):
        self.assertIsNone(self.discover({"PATH": self.empty_path}))


class ImportPathTest(unittest.TestCase):
    def test_import_dir_in_proto_pythonpath(self):
        with tempfile.TemporaryDirectory() as import_dir:
            env = run_conformity.build_env({"PROTO_PYTHONPATH": "/extra"}, import_dir=import_dir)
            self.assertEqual(env["PROTO_PYTHONPATH"], import_dir + os.pathsep + "/extra")
            self.assertNotIn("PYTHONPATH", env)

    def test_marker_value_is_not_a_directory(self):
        with tempfile.TemporaryDirectory() as import_dir:
            env = run_conformity.build_env({"PROTO_PYTHONPATH": "1"}, import_dir=import_dir)
            self.assertEqual(env["PROTO_PYTHONPATH"], import_dir)


if __name__ == "__main__":
    unittest.main()
