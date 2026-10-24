import threading
import time

flag = []
spins = [0]


def spin():
    n = 0
    while not flag:
        n += 1
    spins[0] = n


def keep(tag):
    mine = [tag * 3 for _ in range(2000)]
    count = len(mine)
    print("kept", count, flush=True)
    return count


def churn():
    total = 0
    for i in range(300000):
        total += len([i, i + 1, str(i)])
    print("churned", total, flush=True)


mode = __import__("sys").argv[1:]
if "gc" in mode:
    k = threading.Thread(target=keep, args=("ab",), name="keeper")
    k.start()
    churn()
    k.join()
else:
    s = threading.Thread(target=spin, name="spinner")
    s.start()
    print("spinning", flush=True)
    ticks = 0
    while s.is_alive():
        ticks += 1
        print("tick", ticks, flush=True)
        time.sleep(0.05)
    print("spun", spins[0] > 0)
