#!/usr/bin/env python3
"""
Run the protoPython conformity tests.

The scripts to run are read from bootstrap/cpython_bootstrap.txt (one path per
line, relative to the repository root). When the manifest is missing or empty,
every test under tests/conformity/builtins and tests/conformity/import is run.

The protopy binary is taken from the first of:
  1. the PROTO_PYTHON environment variable;
  2. build_release/src/runtime/protopy in the repository;
  3. build/src/runtime/protopy in the repository;
  4. protopy on PATH.

Each script runs from the repository root with tests/conformity/import
prepended to PROTO_PYTHONPATH, the module search path protopy reads
(protopy does not read PYTHONPATH).

Exit status: 0 when every script passes, 1 when any fails, 2 when no protopy
binary is found.
"""
from __future__ import print_function

import os
import shutil
import subprocess
import sys

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CONFORMITY_DIR = os.path.join(REPO_ROOT, "tests", "conformity")
BOOTSTRAP = os.path.join(CONFORMITY_DIR, "bootstrap", "cpython_bootstrap.txt")
IMPORT_DIR = os.path.join(CONFORMITY_DIR, "import")

# Build directories searched for protopy, relative to the repository root.
BUILD_DIRS = ("build_release", "build")


def _is_executable(path):
    return os.path.isfile(path) and os.access(path, os.X_OK)


def get_proto_python(environ=None, repo_root=REPO_ROOT):
    """Return the protopy binary to test, or None if none is found."""
    if environ is None:
        environ = os.environ
    exe = environ.get("PROTO_PYTHON")
    if exe:
        return exe
    for build_dir in BUILD_DIRS:
        candidate = os.path.join(repo_root, build_dir, "src", "runtime", "protopy")
        if _is_executable(candidate):
            return candidate
    return shutil.which("protopy", path=environ.get("PATH", os.defpath))


def build_env(environ=None, import_dir=IMPORT_DIR):
    """Return the environment for one test script.

    import_dir is prepended to PROTO_PYTHONPATH. An inherited value of "1"
    is dropped: it is the marker protopy exports to signal that code runs
    under protoPython, not a directory.
    """
    if environ is None:
        environ = os.environ
    env = dict(environ)
    entries = [p for p in env.get("PROTO_PYTHONPATH", "").split(os.pathsep)
               if p and p != "1"]
    if os.path.isdir(import_dir):
        entries.insert(0, import_dir)
    if entries:
        env["PROTO_PYTHONPATH"] = os.pathsep.join(entries)
    else:
        env.pop("PROTO_PYTHONPATH", None)
    return env


def run_script(prog, script_path):
    script_path = os.path.normpath(script_path)
    if not os.path.isabs(script_path):
        script_path = os.path.join(REPO_ROOT, script_path)
    if not os.path.isfile(script_path):
        return False, "file not found: %s" % script_path
    try:
        result = subprocess.run(
            [prog, script_path],
            cwd=REPO_ROOT,
            capture_output=True,
            timeout=30,
            env=build_env(),
        )
        output = (result.stdout or b"").decode("utf-8", "replace")
        if result.returncode != 0:
            return False, output + (result.stderr or b"").decode("utf-8", "replace")
        return True, output
    except subprocess.TimeoutExpired:
        return False, "timeout"
    except Exception as e:
        return False, str(e)


def main():
    proto = get_proto_python()
    if not proto:
        print("ERROR: PROTO_PYTHON is not set and no protopy binary was found "
              "(looked in %s and on PATH)"
              % ", ".join(os.path.join(d, "src", "runtime", "protopy") for d in BUILD_DIRS),
              file=sys.stderr)
        sys.exit(2)
    print("Using", proto)

    # Run from bootstrap manifest if present
    tests = []
    if os.path.isfile(BOOTSTRAP):
        with open(BOOTSTRAP, "r") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                # first token is path
                path = line.split()[0]
                if path.endswith(".py"):
                    tests.append(path)

    if not tests:
        # Fallback: discover builtins and import
        for sub in ("builtins", "import"):
            d = os.path.join(CONFORMITY_DIR, sub)
            if os.path.isdir(d):
                for f in sorted(os.listdir(d)):
                    if f.endswith(".py") and f != "conformity_dummy.py":
                        tests.append(os.path.join(d, f))

    failed = []
    for t in tests:
        ok, out = run_script(proto, t)
        if ok:
            print("[PASS]", t)
        else:
            print("[FAIL]", t)
            if out:
                print(out[:500])
            failed.append(t)

    if failed:
        print("Failed:", len(failed), "of", len(tests))
        sys.exit(1)
    print("All", len(tests), "conformity tests passed.")
    sys.exit(0)


if __name__ == "__main__":
    main()
