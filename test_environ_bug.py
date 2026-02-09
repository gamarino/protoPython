
import os
print("Imported os")
print("Initial environ keys: %d" % len(os.environ))
os.environ["PROTO_TEST"] = "working"
print("After assignment, PROTO_TEST in environ: %s" % ("PROTO_TEST" in os.environ))
print("os.getenv('PROTO_TEST'): %s" % os.getenv("PROTO_TEST"))
