# At exit the interpreter joins every non-daemon thread, as CPython's
# threading._shutdown does.  protopy only polled the running-thread count
# for at most 5 s, so a worker still busy after that lost its work, and one
# that had just left the count could still be using the process space when
# protopy destroyed it (issue #54: "mutex lock failed" aborts on macOS).
#
# The pass line is printed by the non-daemon worker after the main module
# has finished, and later than the old 5 s poll: it appears only if exit
# joins the worker.  The daemon thread must not be joined (the test would
# hit its ctest timeout).
import sys
import threading
import time


def slow():
    time.sleep(5.5)
    print("exit_joins_non_daemon_threads: ok", flush=True)


def daemon():
    time.sleep(1000)


threading.Thread(target=slow).start()
threading.Thread(target=daemon, daemon=True).start()
print("main module done", flush=True)
