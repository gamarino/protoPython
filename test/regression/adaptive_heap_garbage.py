# Allocates short-lived objects and keeps none of them: the garbage for the
# adaptive heap tests (adaptive_heap_check.py, CMakeLists.txt).  The first
# argument is the number of rounds; each round builds a list of small lists
# with explicit appends and drops it.  The shape is deliberate: under
# protoCore's heap trace the live set of this loop stays flat (about 0.35 M
# cells), whereas tuples are interned for the life of the process, and list
# comprehensions and chains of class instances were seen to raise the live
# set from cycle to cycle (2026-10-10, not yet explained), which would make a
# bounded-memory check measure something else.  The final line is the
# sentinel the check requires, with the work done, so a run that stopped
# early cannot pass.
import sys


def main():
    rounds = int(sys.argv[1]) if len(sys.argv) > 1 else 20000
    made = 0
    for i in range(rounds):
        batch = []
        for j in range(50):
            batch.append([i, j])
        made += len(batch)
    print("adaptive heap garbage done: rounds=%d objects=%d" % (rounds, made))


main()
