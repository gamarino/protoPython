# Write to a file so we see output even if stdout/stderr are redirected.
import sys
with open("/tmp/protopy_debug.txt", "w") as f:
    f.write("start\n")
    try:
        import _thread
        f.write("_thread imported\n")
        _thread.log_thread_ident("main_from_py")
        f.write("log_thread_ident(main) done\n")
    except Exception as e:
        f.write("_thread error: %s\n" % e)
    try:
        import threading
        f.write("_has_thread=%s\n" % getattr(threading, "_has_thread", None))
    except Exception as e:
        f.write("threading error: %s\n" % e)
    f.write("end\n")
