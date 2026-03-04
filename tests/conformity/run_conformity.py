#!/usr/bin/env python3
"""
Run Phase 1 conformity tests for protoPython.
Reads bootstrap manifest or runs all tests under tests/conformity/builtins and tests/conformity/import.
Set PROTO_PYTHON to the protoPython binary path; else uses first-found protoPython or fails.
"""
from __future__ import print_function

import os
import sys
import subprocess

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CONFORMITY_DIR = os.path.join(REPO_ROOT, "tests", "conformity")
BOOTSTRAP = os.path.join(CONFORMITY_DIR, "bootstrap", "cpython_bootstrap.txt")


def get_proto_python():
    exe = os.environ.get("PROTO_PYTHON")
    if exe:
        return exe
    for name in ("protoPython", "proto python"):
        for d in (REPO_ROOT, os.path.join(REPO_ROOT, "build"), os.path.curdir):
            p = os.path.join(d, name)
            if os.path.isfile(p) and os.access(p, os.X_OK):
                return p
    return None


def run_script(prog, script_path):
    script_path = os.path.normpath(script_path)
    if not os.path.isabs(script_path):
        script_path = os.path.join(REPO_ROOT, script_path)
    if not os.path.isfile(script_path):
        return False, "file not found: %s" % script_path
    env = dict(os.environ)
    import_dir = os.path.join(CONFORMITY_DIR, "import")
    if os.path.isdir(import_dir):
        env["PYTHONPATH"] = import_dir + os.pathsep + env.get("PYTHONPATH", "")
    try:
        result = subprocess.run(
            [prog, script_path],
            cwd=REPO_ROOT,
            capture_output=True,
            timeout=30,
            env=env,
        )
        if result.returncode != 0:
            return False, (result.stdout or b"").decode("utf-8", "replace") + (result.stderr or b"").decode("utf-8", "replace")
        return True, (result.stdout or b"").decode("utf-8", "replace")
    except subprocess.TimeoutExpired:
        return False, "timeout"
    except Exception as e:
        return False, str(e)


def main():
    proto = get_proto_python()
    if not proto:
        print("ERROR: PROTO_PYTHON not set and protoPython binary not found", file=sys.stderr)
        sys.exit(2)

    # Run from bootstrap manifest if present
    tests = []
    if os.path.isfile(BOOTSTRAP):
        with open(BOOTSTRAP, "r") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                # first token is path
                path = line.split()[0] if line.split() else line
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
