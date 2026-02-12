import _thread
import time

def thread_func(name):
    print("Thread started")
    import os
    import sys
    import math
    print("Thread finished")

for i in range(5):
    _thread.start_new_thread(thread_func, (i,))

time.sleep(5)
print("Main thread finishing")
