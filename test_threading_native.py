print("DEBUG: started test_threading_native.py")
import _thread
print("DEBUG: imported _thread")
import threading
print("DEBUG: imported threading")
import time
print("DEBUG: imported time")
print("DEBUG: __name__ is", __name__)

def test_locks():
    print("Testing Lock...")
    l = threading.Lock()
    print("Locked initially:", l.locked())
    l.acquire()
    print("Locked after acquire:", l.locked())
    l.release()
    print("Locked after release:", l.locked())
    
    print("\nTesting context manager surrogate (try-finally)...")
    l.acquire()
    try:
        print("Inside lock, locked:", l.locked())
    finally:
        l.release()
    print("Outside lock, locked:", l.locked())

def test_rlocks():
    print("\nTesting RLock...")
    rl = threading.RLock()
    print("RLock initially locked:", rl.locked())
    rl.acquire()
    print("RLock after first acquire, locked:", rl.locked())
    rl.acquire()
    print("RLock after second acquire (same thread), locked:", rl.locked())
    rl.release()
    print("RLock after first release, locked:", rl.locked())
    rl.release()
    print("RLock after second release, locked:", rl.locked())

def test_threads():
    print("\nTesting Thread...")
    def worker(name, delay):
        print("Worker {} starting...".format(name))
        time.sleep(delay)
        print("Worker {} finished.".format(name))

    t1 = threading.Thread(worker, ("A", 0.05))
    t2 = threading.Thread(worker, ("B", 0.1))
    
    t1.start()
    t2.start()
    
    print("Active count:", threading.active_count())
    
    t1.join()
    t2.join()
    print("Threads joined.")

if __name__ == "__main__":
    test_locks()
    test_rlocks()
    test_threads()
    print("\nBatch 2 basic verification done.")
