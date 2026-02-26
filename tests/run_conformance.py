import os
import subprocess
import time
import sys

BASE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PROTOPY = os.path.join(BASE_DIR, "cmake-build-asan", "src", "runtime", "protopy")
if not os.path.exists(PROTOPY):
    PROTOPY = os.path.join(BASE_DIR, "cmake-build-debug", "src", "runtime", "protopy")
if not os.path.exists(PROTOPY):
    PROTOPY = os.path.join(BASE_DIR, "cmake-build-release", "src", "runtime", "protopy")

if sys.platform == "win32" and not PROTOPY.endswith(".exe"):
    PROTOPY += ".exe"

LOG = os.path.join(BASE_DIR, "tests", "conformance_status.log")
TIMEOUT = 120

ESSENTIAL = [
    "test/cpython/test_grammar.py",
    "test/cpython/test_types.py",
    "test/cpython/test_descr.py",
    "test/cpython/test_generators.py",
    "test/cpython/test_asyncgen.py",
    "test/cpython/test_base64.py"
]

IMPORTANT = [
    "tests/test_os.py"
]

NECESSARY = [
    "tests/test_decorator.py"
]

def run_tests(group_name, tests):
    with open(LOG, "a") as f:
        print(f"## Running {group_name} tests...", file=f)
        print(f"## Running {group_name} tests...")
        
        for test_file in tests:
            full_test_path = os.path.join(BASE_DIR, test_file.replace("/", os.sep))
            if not os.path.isfile(full_test_path):
                msg = f"[SKIP] {test_file} (Not Found)"
                print(msg, file=f)
                print(msg)
                continue
                
            msg = f"[RUN] {test_file} ... "
            print(msg, end="", file=f)
            print(msg, end="")
            
            start_time = time.time()
            STDLIB_PATH = os.path.join(BASE_DIR, "lib", "python3.14")
            env = os.environ.copy()
            env["PROTO_PYTHONPATH"] = STDLIB_PATH
            env["PROTO_ENV_DIAG"] = "1"
            
            try:
                proc = subprocess.run([PROTOPY, full_test_path], env=env, timeout=TIMEOUT, capture_output=True)
                ret = proc.returncode
            except subprocess.TimeoutExpired:
                ret = 124
            except Exception as e:
                print(e)
                ret = -1
                
            duration = int(time.time() - start_time)
            
            if ret == 0:
                res = f"PASS ({duration}s)"
                print(res, file=f)
                print(res)
            elif ret == 124:
                res = f"TIMEOUT ({duration}s)"
                print(res, file=f)
                print(res)
            else:
                res = f"FAIL (exit {ret}, {duration}s)"
                print(res, file=f)
                print(res)
        print("", file=f)
        print("")

with open(LOG, "w") as f:
    f.write(f"--- Conformance Suite Started ---\n")
    f.write(f"Timeout per test: {TIMEOUT}s\n\n")

run_tests("Essential", ESSENTIAL)
run_tests("Important", IMPORTANT)
run_tests("Necessary", NECESSARY)

with open(LOG, "a") as f:
    f.write(f"--- Conformance Suite Finished ---\n")
