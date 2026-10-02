# Several threads read sys.stdin concurrently: every readline() call returns a
# whole line, and every line is returned exactly once. Run with standard input
# redirected from a file of LINES numbered lines (cli_stdin.cmake writes it);
# each line is "<number>:" followed by a fixed filler, long enough that lines
# straddle the 8192-byte read-ahead blocks.
#
# sys.stdin's read-ahead buffer is shared by every thread; without a lock
# around it, two threads could both take the same buffered bytes, or one
# could take half a line while another refills the buffer.

import sys
import threading

LINES = int(sys.argv[1])
THREADS = 8
FILLER = "x" * 90

results = [[] for _ in range(THREADS)]


def reader(slot):
    out = results[slot]
    while True:
        line = sys.stdin.readline()
        if not line:
            return
        out.append(line)


workers = [threading.Thread(target=reader, args=(k,)) for k in range(THREADS)]
for w in workers:
    w.start()
for w in workers:
    w.join()

seen = set()
total = 0
for out in results:
    for line in out:
        total += 1
        number, sep, rest = line.partition(":")
        if sep != ":" or rest != FILLER + "\n" or not number.isdigit():
            print("torn line: %r" % line)
            sys.exit(1)
        if number in seen:
            print("line returned twice: %s" % number)
            sys.exit(1)
        seen.add(number)
if total != LINES or len(seen) != LINES:
    print("expected %d lines, got %d (%d distinct)" % (LINES, total, len(seen)))
    sys.exit(1)
print("stdin_threads: %d whole lines" % LINES)
