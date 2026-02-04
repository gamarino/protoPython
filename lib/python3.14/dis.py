"""
Minimal dis stub for protoPython.
dis, disassemble, opname; full implementation requires bytecode introspection and opcode tables.
"""

# Stub: empty list. Full impl requires CPython opcode names.
opname = []

def dis(x=None, *, file=None, depth=None):
    """Stub: no-op. Full impl requires bytecode disassembly and output."""
    pass

def disassemble(co, lasti=-1, *, file=None):
    """Stub: no-op. Full impl requires code object introspection."""
    pass

def distb(tb=None, *, file=None):
    """Stub: no-op. Full impl requires traceback disassembly."""
    pass
