
import sys

opmap = {
    'LOAD_CONST': 100,
    'RETURN_VALUE': 101,
    'LOAD_NAME': 102,
    'STORE_NAME': 103,
    'BINARY_ADD': 104,
    'BINARY_SUBTRACT': 105,
    'CALL_FUNCTION': 106,
    'BINARY_MULTIPLY': 107,
    'BINARY_TRUE_DIVIDE': 108,
    'COMPARE_OP': 109,
    'PJUMP_IF_FALSE': 110,
    'JUMP_ABSOLUTE': 111,
    'LOAD_ATTR': 112,
    'STORE_ATTR': 113,
    'BUILD_LIST': 114,
    'BINARY_SUBSCR': 115,
    'BUILD_MAP': 116,
    'STORE_SUBSCR': 117,
    'BUILD_TUPLE': 118,
    'GET_ITER': 119,
    'FOR_ITER': 120,
    'UNPACK_SEQUENCE': 121,
    'LOAD_GLOBAL': 122,
    'STORE_GLOBAL': 123,
    'BUILD_SLICE': 124,
    'ROT_TWO': 125,
    'DUP_TOP': 126,
    'BINARY_MODULO': 127,
    'BINARY_POWER': 128,
    'BINARY_FLOOR_DIVIDE': 129,
    'UNARY_NEGATIVE': 130,
    'UNARY_NOT': 131,
    'UNARY_INVERT': 132,
    'PTOP': 133,
    'UNARY_POSITIVE': 134,
    'NOP': 135,
    'INPLACE_ADD': 136,
    'BINARY_LSHIFT': 137,
    'BINARY_RSHIFT': 138,
    'INPLACE_SUBTRACT': 139,
    'BINARY_AND': 140,
    'BINARY_OR': 141,
    'BINARY_XOR': 142,
    'INPLACE_MULTIPLY': 143,
    'INPLACE_TRUE_DIVIDE': 144,
    'INPLACE_FLOOR_DIVIDE': 145,
    'INPLACE_MODULO': 146,
    'INPLACE_POWER': 147,
    'INPLACE_LSHIFT': 148,
    'INPLACE_RSHIFT': 149,
    'INPLACE_AND': 150,
    'INPLACE_OR': 151,
    'INPLACE_XOR': 152,
    'ROT_THREE': 153,
    'ROT_FOUR': 154,
    'DUP_TTWO': 155,
    'BUILD_FUNCTION': 156,
    'LOAD_FAST': 157,
    'STORE_FAST': 158,
    'CALL_FUNCTION_KW': 159,
    'BUILD_CLASS': 160,
    'DELETE_NAME': 161,
    'DELETE_ATTR': 162,
    'DELETE_SUBSCR': 163,
    'DELETE_GLOBAL': 164,
    'DELETE_FAST': 165,
    'RAISE_VARARGS': 166,
    'LIST_APPEND': 168,
    'MAP_ADD': 169,
    'SET_ADD': 170,
    'BUILD_SET': 171,
    'YIELD_VALUE': 172,
    'SETUP_WITH': 173,
    'WITH_CLEANUP': 174,
    'GET_YIELD_FROM_ITER': 175,
    'YIELD_FROM': 176,
    'SETUP_FINALLY': 177,
    'PBLOCK': 178,
    'BUILD_STRING': 179,
    'LOAD_DEREF': 180,
    'STORE_DEREF': 181,
    'CALL_FUNCTION_EX': 182,
    'LIST_EXTEND': 183,
    'DICT_UPDATE': 184,
    'SET_UPDATE': 185,
    'LIST_TO_TUPLE': 186,
    'GET_AWAITABLE': 187,
    'GET_AITER': 188,
    'GET_ANEXT': 189,
    'EXCEPTION_MATCH': 190,
    'SETUP_ASYNC_WITH': 191,
    'BINARY_MATRIX_MULTIPLY': 192,
    'INPLACE_MATRIX_MULTIPLY': 193,
    'RERAISE': 194,
    'JUMP_FORWARD': 195,
    'FORMAT_VALUE': 196,
    'GEN_START': 197,
    'GET_LEN': 198,
    'MATCH_MAPPING': 199,
    'MATCH_SEQUENCE': 200,
    'EXTENDED_ARG': 201,
    'PEXCEPT': 202,
    'IMPORT_STAR': 203,
    'PJUMP_IF_TRUE': 204,
    'UNPACK_EX': 205,
    'IMPORT_FROM': 206,
}

opname = ['<%r>' % (op,) for op in range(256)]
for name, val in opmap.items():
    if val < len(opname):
        opname[val] = name

def get_instructions(co):
    code = list(co.co_code)
    for i in range(0, len(code), 2):
        op = code[i]
        arg = code[i+1]
        yield (i, opname[op], arg)

def dis(x=None):
    if x is None:
        return
    if hasattr(x, '__code__'):
        co = x.__code__
    elif hasattr(x, 'gi_code'):
        co = x.gi_code
    elif hasattr(x, 'co_code'):
        co = x
    else:
        print("Cannot disassemble", type(x))
        return

    code = list(co.co_code)
    for i in range(0, len(code), 2):
        op = code[i]
        arg = code[i+1]
        name = opname[op]
        print(f"{i//2:4} {name:<20} {arg}")

