import _collections_abc
import sys
def log(m): sys.stderr.write(str(m) + "\n"); sys.stderr.flush()

log(f"sys.path: {sys.path}")
try:
    import argparse
    log(f"ArgumentParser MRO: {argparse.ArgumentParser.__mro__}")
    log(f"Has add_argument: {hasattr(argparse.ArgumentParser, 'add_argument')}")
    parser = argparse.ArgumentParser(description="test")
    log(f"Has description: {hasattr(parser, 'description')}")
    log("argparse check success")
except Exception as e:
    log(f"Error: {e}")
    import traceback
    log(f"traceback object: {traceback}")
    log(f"traceback type: {type(traceback)}")
    if hasattr(traceback, 'print_exc'):
        traceback.print_exc(file=sys.stderr)
    else:
        log("traceback module MISSING print_exc!")

