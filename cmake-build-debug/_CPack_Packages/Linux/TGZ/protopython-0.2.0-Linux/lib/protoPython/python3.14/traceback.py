# traceback.py - Minimal traceback module.

import sys

def extract_tb(tb, limit=None):
    """Extract list of (filename, lineno, name, line) from traceback. Stub returns []."""
    return []

def format_list(extracted_list):
    """Format extracted traceback entries as list of strings."""
    out = []
    for (f, l, n, line) in extracted_list:
        s = '  File "%s", line %s, in %s\n' % (f, l, n)
        if line:
            s += '    %s\n' % line
        out.append(s)
    return out

def format_exception(etype, value, tb, limit=None, chain=True):
    """Format exception as list of strings."""
    lines = []
    if etype:
        n = getattr(etype, '__name__', str(etype))
        lines.append("%s: %s\n" % (n, str(value) if value is not None else ""))
    return lines

def format_exc(limit=None, chain=True):
    """Format current exception. Stub returns empty string (no sys.exc_info)."""
    return ""

def print_exc(limit=None, file=None, chain=True):
    """Print exception to file (default sys.stderr)."""
    s = format_exc(limit=limit, chain=chain)
    if s:
        (file or sys.stderr).write(s)

def print_exception(etype, value, tb, limit=None, file=None, chain=True):
    """Print exception from (etype, value, tb)."""
    lines = format_exception(etype, value, tb, limit=limit, chain=chain)
    out = file or sys.stderr
    for line in lines:
        out.write(line)
