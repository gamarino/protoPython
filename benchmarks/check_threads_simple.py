# Write result to a file so we don't depend on print.
import sys
out = "/tmp/protopy_check.txt"
try:
    with open(out, "w") as f:
        f.write("start\n")
        import threading
        has = getattr(threading, "_has_thread", False)
        f.write("_has_thread=%s\n" % has)
        if has:
            f.write("get_ident=%s getpid=%s\n" % (threading.get_ident(), threading.getpid()))
        f.write("end\n")
except Exception as e:
    with open(out, "w") as f:
        f.write("error: %s\n" % e)
