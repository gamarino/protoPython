# _thread.get_ident is the thread's identity: the value start_new_thread
# reports for the thread, the main thread's for the main thread, and the
# owner RLock checks against.  (protoPython takes it from the ProtoThread,
# not from an OS thread call.)
import _thread
import threading

res = {}
lock = threading.RLock()

def work():
    res["ident"] = threading.get_ident()
    res["current"] = threading.current_thread()
    try:
        lock.release()
    except RuntimeError:
        res["foreign_release"] = "refused"
    with lock:
        with lock:
            res["reentrant"] = lock._is_owned()

lock.acquire()
t = threading.Thread(target=work)
t.start()
lock.release()
t.join()
assert res["ident"] == t.ident, (res["ident"], t.ident)
assert res["ident"] != threading.get_ident()
assert res["current"] is t
assert res["foreign_release"] == "refused"
assert res["reentrant"]
assert threading.main_thread().ident == threading.get_ident() == _thread._get_main_thread_ident() \
    if hasattr(_thread, "_get_main_thread_ident") else threading.main_thread().ident == threading.get_ident()
assert lock._is_owned() is False
print("thread_ident: ok")
