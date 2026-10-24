import threading


def worker(k):
    total = k * 100
    for i in range(3):
        total += i
    print("worker", k, total, flush=True)


t1 = threading.Thread(target=worker, args=(1,))
t2 = threading.Thread(target=worker, args=(2,))
t1.start()
t2.start()
t1.join()
t2.join()
print("done")
