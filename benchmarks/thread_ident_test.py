#!/usr/bin/env python3
# thread_ident_test.py - Log PID and TID for main and each worker via _thread.log_thread_ident (C stderr).
# Run: protopy --path lib/python3.14 --script benchmarks/thread_ident_test.py 2>&1
# Expect: same PID for all, different TIDs for main vs workers.

import sys


def worker(index):
    try:
        import _thread
        _thread.log_thread_ident("worker %d" % index)
    except Exception:
        pass
    return index


def main():
    import threading
    has_thread = getattr(threading, "_has_thread", False)
    try:
        import _thread
        _thread.log_thread_ident("main (_has_thread=%s)" % has_thread)
    except Exception:
        pass
    if not has_thread:
        return 0
    threads = []
    for i in range(4):
        t = threading.Thread(target=worker, args=(i,))
        threads.append(t)
        t.start()
    for t in threads:
        t.join()
    try:
        import _thread
        _thread.log_thread_ident("main (after join)")
    except Exception:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
