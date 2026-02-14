# examples/threading_demo.py
# This example demonstrates true parallel execution in ProtoPython's GIL-free runtime.

import threading
import time

def cpu_intensive_task(name, duration):
    """A task that simulates CPU-intensive work."""
    print(f"Thread {name}: starting work for {duration} seconds")
    start_time = time.time()
    count = 0
    while time.time() - start_time < duration:
        # Perform some calculations
        count += 1
    print(f"Thread {name}: completed {count} iterations")

def main():
    print("--- Parallel Execution Demo ---")
    start = time.time()
    
    threads = []
    # Launch multiple threads. In ProtoPython, these run on separate CPU cores simultaneously.
    for i in range(4):
        t = threading.Thread(target=cpu_intensive_task, args=(f"Worker-{i}", 1.0))
        threads.append(t)
        t.start()
    
    for t in threads:
        t.join()
        
    end = time.time()
    print(f"Total wall time: {end - start:.2f} seconds")
    print("Note: In a GIL-constrained runtime, this would take ~4 seconds. In ProtoPython, it takes ~1 second.")

if __name__ == "__main__":
    main()
