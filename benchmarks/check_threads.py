"""Quick check: print threading state and thread ids to stdout."""
import sys
print("check_threads: start", flush=True)
try:
    import threading
    has = getattr(threading, "_has_thread", False)
    print("_has_thread =", has, flush=True)
    print("get_ident() =", threading.get_ident(), flush=True)
    print("getpid() =", threading.getpid(), flush=True)
    if has:
        def f(i):
            print("  worker", i, "tid =", threading.get_ident(), flush=True)
        threads = []
        for i in range(3):
            t = threading.Thread(target=f, args=(i,))
            threads.append(t)
            t.start()
        print("main: waiting for threads (join)...", flush=True)
        for t in threads:
            t.join()
        print("main: all joined.", flush=True)
except Exception as e:
    print("error:", e, flush=True)
print("check_threads: done", flush=True)
