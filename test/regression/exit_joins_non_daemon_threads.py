# At exit the interpreter joins every non-daemon thread, as CPython's
# threading._shutdown does.  protopy only polled the running-thread count
# for at most 5 s, so a worker still busy after that lost its work, and one
# that had just left the count could still be using the process space when
# protopy destroyed it (issue #54: "mutex lock failed" aborts on macOS).
import subprocess
import sys

CHILD = r'''
import threading, time

def slow():
    time.sleep(5.5)          # longer than the old 5 s poll
    print("non-daemon worker done", flush=True)

def daemon():
    time.sleep(1000)

threading.Thread(target=slow).start()
threading.Thread(target=daemon, daemon=True).start()
print("main done", flush=True)
'''

r = subprocess.run([sys.executable, "-c", CHILD], capture_output=True, timeout=60)
out = r.stdout.decode().splitlines()
assert r.returncode == 0, (r.returncode, r.stderr)
assert out == ["main done", "non-daemon worker done"], (out, r.stderr)
print("exit_joins_non_daemon_threads: ok")
