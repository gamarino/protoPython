# os.py - Minimal stub for protoPython.
# environ: empty dict; full implementation would require native getenv/environ.
# getcwd, chdir, path, getpid: stub values or no-op. See STUBS.md.
import sys

name = 'os'

environ = {}  # Stub: empty dict; full impl requires native getenv.
path = []     # Stub: empty path list.
pathsep = ':'
sep = '/'

def getpid():
    return 0  # Placeholder

def getcwd():
    return "."  # Stub

def chdir(path):
    pass  # Stub: no-op
