import faulthandler
faulthandler.enable()

import threading
import time
import sys
import os

def dumper():
    time.sleep(3)
    print("Dumping traceback...", flush=True)
    faulthandler.dump_traceback(file=sys.stderr)
    os._exit(1)

t = threading.Thread(target=dumper)
t.start()

print("Importing functools...", flush=True)
import functools
