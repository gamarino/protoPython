# getopt.py - Minimal stub. getopt(args, shortopts, longopts=[]) so imports work.

def getopt(args, shortopts, longopts=None):
    """Stub: returns empty list of options and remaining args."""
    if longopts is None:
        longopts = []
    return [], list(args)
