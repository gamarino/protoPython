import threading
import time

counter = 0
lock = threading.Lock()

def incrementer(n):
    global counter
    for _ in range(n):
        lock.acquire()
        try:
            c = counter
            time.sleep(0.0001) # Force context switch
            counter = c + 1
        finally:
            lock.release()

def test_lock_contention():
    global counter
    counter = 0
    print("Testing lock contention with 10 threads...")
    threads = []
    iterations = 100
    for i in range(10):
        print("Starting thread {}...".format(i))
        t = threading.Thread(incrementer, (iterations,))
        threads.append(t)
        t.start()
        print("Thread {} started.".format(i))
    
    for t in threads:
        t.join()
    
    print("Final counter: {} (expected {})".format(counter, 10 * iterations))
    if counter == 10 * iterations:
        print("Lock contention test passed.")
    else:
        print("Lock contention test FAILED.")

if __name__ == "__main__":
    test_lock_contention()
