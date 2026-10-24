import os
import threading

done = []


def churn():
    total = 0
    for i in range(400000):
        total += len([i, i + 1, str(i)])
    done.append(total)


t = threading.Thread(target=churn)
t.start()
limit = os.environ.get("PROTOCORE_HEAP_LIMIT_CELLS")
t.join()
print("churned", done[0], limit)
