"""
dis: bytecode disassembler for protoPython code objects.
Code objects have co_consts, co_names, co_code (flat list: opcode, [arg], ...).
"""

# ProtoPython opcode names; index matches opcode value (100-155).
opname = ['' ] * 156
def _set(start, names):
    for i, n in enumerate(names):
        if start + i < len(opname):
            opname[start + i] = n
_set(100, [
    'LOAD_CONST', 'RETURN_VALUE', 'LOAD_NAME', 'STORE_NAME', 'BINARY_ADD', 'BINARY_SUBTRACT',
    'CALL_FUNCTION', 'BINARY_MULTIPLY', 'BINARY_TRUE_DIVIDE', 'COMPARE_OP', 'POP_JUMP_IF_FALSE',
    'JUMP_ABSOLUTE', 'LOAD_ATTR', 'STORE_ATTR', 'BUILD_LIST', 'BINARY_SUBSCR', 'BUILD_MAP',
    'STORE_SUBSCR', 'BUILD_TUPLE', 'GET_ITER', 'FOR_ITER', 'UNPACK_SEQUENCE', 'LOAD_GLOBAL',
    'STORE_GLOBAL', 'BUILD_SLICE', 'ROT_TWO', 'DUP_TOP', 'BINARY_MODULO', 'BINARY_POWER',
    'BINARY_FLOOR_DIVIDE', 'UNARY_NEGATIVE', 'UNARY_NOT', 'UNARY_INVERT', 'POP_TOP',
    'UNARY_POSITIVE', 'NOP', 'INPLACE_ADD', 'BINARY_LSHIFT', 'BINARY_RSHIFT', 'INPLACE_SUBTRACT',
    'BINARY_AND', 'BINARY_OR', 'BINARY_XOR', 'INPLACE_MULTIPLY', 'INPLACE_TRUE_DIVIDE',
    'INPLACE_FLOOR_DIVIDE', 'INPLACE_MODULO', 'INPLACE_POWER', 'INPLACE_LSHIFT', 'INPLACE_RSHIFT',
    'INPLACE_AND', 'INPLACE_OR', 'INPLACE_XOR', 'ROT_THREE', 'ROT_FOUR', 'DUP_TOP_TWO'
])

# Opcodes that consume an argument (next word in bytecode).
_HAS_ARG = frozenset([
    100, 102, 103, 106, 109, 110, 111, 112, 113, 114, 116, 117, 118, 120, 121, 122, 123, 124,
    137, 138, 139, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152
])

def _get_code_attr(co, name, default=None):
    if hasattr(co, 'get'):
        return co.get(name, default)
    return getattr(co, name, default) if hasattr(co, name) else default

def _get_co_code(co):
    return _get_code_attr(co, 'co_code')

def _get_co_consts(co):
    return _get_code_attr(co, 'co_consts')

def _get_co_names(co):
    return _get_code_attr(co, 'co_names')

def _int_val(obj, default=0):
    try:
        return int(obj)
    except (TypeError, ValueError):
        return default

def disassemble(co, lasti=-1, *, file=None):
    """Disassemble a code object (co_code, co_consts, co_names)."""
    code = _get_co_code(co)
    consts = _get_co_consts(co)
    names = _get_co_names(co)
    if code is None:
        return
    try:
        code_list = list(code)
    except TypeError:
        return
    out = file
    if out is None:
        try:
            import sys
            out = sys.stdout
        except Exception:
            return
    i = 0
    n = len(code_list)
    while i < n:
        op = _int_val(code_list[i], -1)
        arg = 0
        if i + 1 < n and op in _HAS_ARG:
            arg = _int_val(code_list[i + 1], 0)
            i += 2
        else:
            i += 1
        name = opname[op] if 0 <= op < len(opname) and opname[op] else ('<%d>' % op)
        line = '%4d  %s' % (i - (2 if op in _HAS_ARG else 1), name)
        if op in _HAS_ARG:
            if op == 100 and consts is not None and 0 <= arg < len(consts):
                line += ' %s' % repr(consts[arg])
            elif op in (102, 103, 112, 113, 122, 123) and names is not None and 0 <= arg < len(names):
                line += ' %s' % repr(names[arg])
            else:
                line += ' %s' % arg
        line += '\n'
        if hasattr(out, 'write'):
            out.write(line)

def dis(x=None, *, file=None, depth=None):
    """Disassemble x (code object, function, or None for last traceback)."""
    if x is None:
        return
    co = _get_code_attr(x, '__code__') or x
    if co is not None:
        disassemble(co, file=file)

def distb(tb=None, *, file=None):
    """Disassemble a traceback. Stub: no-op if no traceback support."""
    pass
