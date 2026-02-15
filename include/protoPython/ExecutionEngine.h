#ifndef PROTOPYTHON_EXECUTIONENGINE_H
#define PROTOPYTHON_EXECUTIONENGINE_H

#include <protoCore.h>

namespace protoPython {

/** LOAD_CONST: push constants[index] onto stack. */
constexpr int OP_LOAD_CONST = 100;
/** RETURN_VALUE: pop and return top of stack. */
constexpr int OP_RETURN_VALUE = 101;
/** LOAD_NAME: push frame[name] where name = names[index]. */
constexpr int OP_LOAD_NAME = 102;
/** STORE_NAME: pop and set frame[name] where name = names[index]. */
constexpr int OP_STORE_NAME = 103;
/** BINARY_ADD: pop two values, push left + right. */
constexpr int OP_BINARY_ADD = 104;
/** BINARY_SUBTRACT: pop two values, push left - right. */
constexpr int OP_BINARY_SUBTRACT = 105;
/** CALL_FUNCTION: pop n args and callable, invoke callable(args), push result. */
constexpr int OP_CALL_FUNCTION = 106;
/** BINARY_MULTIPLY: pop two values, push left * right. */
constexpr int OP_BINARY_MULTIPLY = 107;
/** BINARY_TRUE_DIVIDE: pop two values, push left / right. */
constexpr int OP_BINARY_TRUE_DIVIDE = 108;
/** COMPARE_OP: arg=0 eq, 1 ne, 2 lt, 3 le, 4 gt, 5 ge. Pop two, push bool. */
constexpr int OP_COMPARE_OP = 109;
/** POP_JUMP_IF_FALSE: if top is falsy, jump to arg (bytecode index). */
constexpr int OP_POP_JUMP_IF_FALSE = 110;
/** JUMP_ABSOLUTE: unconditionally jump to arg (bytecode index). */
constexpr int OP_JUMP_ABSOLUTE = 111;
/** LOAD_ATTR: pop obj, push obj.attr where attr = names[index]. */
constexpr int OP_LOAD_ATTR = 112;
/** STORE_ATTR: pop value, pop obj, set obj.attr = value where attr = names[index]. */
constexpr int OP_STORE_ATTR = 113;
/** BUILD_LIST: pop arg values from stack, build list, push list. */
constexpr int OP_BUILD_LIST = 114;
/** BINARY_SUBSCR: pop index, pop container, push container[index]. */
constexpr int OP_BINARY_SUBSCR = 115;
/** BUILD_MAP: pop 2*arg (value, key) pairs, build dict, push dict. */
constexpr int OP_BUILD_MAP = 116;
/** STORE_SUBSCR: pop value, pop key, pop container; set container[key] = value. */
constexpr int OP_STORE_SUBSCR = 117;
/** BUILD_TUPLE: pop arg values from stack, build tuple, push tuple. */
constexpr int OP_BUILD_TUPLE = 118;
/** GET_ITER: replace TOS with iter(TOS) (call __iter__). */
constexpr int OP_GET_ITER = 119;
/** FOR_ITER: TOS = iterator. Call __next__; if value push it; else pop iterator and jump to instruction index arg. */
constexpr int OP_FOR_ITER = 120;
/** UNPACK_SEQUENCE: pop sequence, push arg elements onto stack (reverse order for correct assignment). */
constexpr int OP_UNPACK_SEQUENCE = 121;
/** LOAD_GLOBAL: push globals[name] (or frame when used as global namespace). */
constexpr int OP_LOAD_GLOBAL = 122;
/** STORE_GLOBAL: pop and set globals[name] = value. */
constexpr int OP_STORE_GLOBAL = 123;
/** BUILD_SLICE: pop arg values (2 or 3: start, stop[, step]), build slice object, push. */
constexpr int OP_BUILD_SLICE = 124;
/** ROT_TWO: swap the two top stack items. */
constexpr int OP_ROT_TWO = 125;
/** DUP_TOP: duplicate the reference on top of the stack. */
constexpr int OP_DUP_TOP = 126;
/** BINARY_MODULO: pop two values, push left % right. */
constexpr int OP_BINARY_MODULO = 127;
/** BINARY_POWER: pop two values, push left ** right. */
constexpr int OP_BINARY_POWER = 128;
/** BINARY_FLOOR_DIVIDE: pop two values, push left // right. */
constexpr int OP_BINARY_FLOOR_DIVIDE = 129;
/** UNARY_NEGATIVE: pop one value, push -TOS. */
constexpr int OP_UNARY_NEGATIVE = 130;
/** UNARY_NOT: pop one value, push not TOS (boolean negation). */
constexpr int OP_UNARY_NOT = 131;
/** UNARY_INVERT: pop one value, push ~TOS (bitwise invert; for int: -x-1). */
constexpr int OP_UNARY_INVERT = 132;
/** POP_TOP: discard the top-of-stack value. */
constexpr int OP_POP_TOP = 133;
/** UNARY_POSITIVE: push +TOS (for int/float same value; optional __pos__). */
constexpr int OP_UNARY_POSITIVE = 134;
/** NOP: no operation (advance instruction pointer only). */
constexpr int OP_NOP = 135;
/** INPLACE_ADD: a += b; for list use __iadd__; else same as BINARY_ADD. */
constexpr int OP_INPLACE_ADD = 136;
/** BINARY_LSHIFT: pop two values, push a << b (int bitwise). */
constexpr int OP_BINARY_LSHIFT = 137;
/** BINARY_RSHIFT: pop two values, push a >> b (int bitwise). */
constexpr int OP_BINARY_RSHIFT = 138;
/** INPLACE_SUBTRACT: a -= b; use __isub__ if present else same as BINARY_SUBTRACT. */
constexpr int OP_INPLACE_SUBTRACT = 139;
/** BINARY_AND: pop two values, push a & b (int bitwise and; else __and__/__rand__). */
constexpr int OP_BINARY_AND = 140;
/** BINARY_OR: pop two values, push a | b (int bitwise or; else __or__/__ror__). */
constexpr int OP_BINARY_OR = 141;
/** BINARY_XOR: pop two values, push a ^ b (int bitwise xor; else __xor__/__rxor__). */
constexpr int OP_BINARY_XOR = 142;
/** INPLACE_MULTIPLY: a *= b; use __imul__ if present else same as BINARY_MULTIPLY. */
constexpr int OP_INPLACE_MULTIPLY = 143;
/** INPLACE_TRUE_DIVIDE: a /= b; use __itruediv__ if present else same as BINARY_TRUE_DIVIDE. */
constexpr int OP_INPLACE_TRUE_DIVIDE = 144;
/** INPLACE_FLOOR_DIVIDE: a //= b; use __ifloordiv__ if present else same as BINARY_FLOOR_DIVIDE. */
constexpr int OP_INPLACE_FLOOR_DIVIDE = 145;
/** INPLACE_MODULO: a %= b; use __imod__ if present else same as BINARY_MODULO. */
constexpr int OP_INPLACE_MODULO = 146;
/** INPLACE_POWER: a **= b; use __ipow__ if present else same as BINARY_POWER. */
constexpr int OP_INPLACE_POWER = 147;
/** INPLACE_LSHIFT: a <<= b; use __ilshift__ if present else same as BINARY_LSHIFT. */
constexpr int OP_INPLACE_LSHIFT = 148;
/** INPLACE_RSHIFT: a >>= b; use __irshift__ if present else same as BINARY_RSHIFT. */
constexpr int OP_INPLACE_RSHIFT = 149;
/** INPLACE_AND: a &= b; use __iand__ if present else same as BINARY_AND. */
constexpr int OP_INPLACE_AND = 150;
/** INPLACE_OR: a |= b; use __ior__ if present else same as BINARY_OR. */
constexpr int OP_INPLACE_OR = 151;
/** INPLACE_XOR: a ^= b; use __ixor__ if present else same as BINARY_XOR. */
constexpr int OP_INPLACE_XOR = 152;
/** ROT_THREE: lift third stack item to top. … C B A → … B A C (A becomes new TOS). */
constexpr int OP_ROT_THREE = 153;
/** ROT_FOUR: lift fourth stack item to top. … D C B A → … C B A D (D becomes new TOS). */
constexpr int OP_ROT_FOUR = 154;
/** DUP_TOP_TWO: duplicate the two top stack items. … A B → … A B A B. */
constexpr int OP_DUP_TOP_TWO = 155;
/** Pop code object from stack, push a callable with __code__ and __globals__ (current frame). */
constexpr int OP_BUILD_FUNCTION = 156;
/** LOAD_FAST: push ctx->getAutomaticLocals()[arg] (O(1) slot load). */
constexpr int OP_LOAD_FAST = 157;
/** STORE_FAST: pop TOS and store in ctx->getAutomaticLocals()[arg]. */
constexpr int OP_STORE_FAST = 158;
/** CALL_FUNCTION_KW: pop name tuple, then kwargs, then positional args, then callable. */
constexpr int OP_CALL_FUNCTION_KW = 159;
/** BUILD_CLASS: pop body_callable, bases_tuple, name; build class, push class. */
constexpr int OP_BUILD_CLASS = 160;
/** DELETE_NAME: delete frame[name] where name = names[index]. */
constexpr int OP_DELETE_NAME = 161;
/** DELETE_ATTR: pop obj, delete obj.attr where attr = names[index]. */
constexpr int OP_DELETE_ATTR = 162;
/** DELETE_SUBSCR: pop key, pop container; delete container[key]. */
constexpr int OP_DELETE_SUBSCR = 163;
/** DELETE_GLOBAL: delete globals[name]. */
constexpr int OP_DELETE_GLOBAL = 164;
/** DELETE_FAST: delete ctx->getAutomaticLocals()[arg]. */
constexpr int OP_DELETE_FAST = 165;
/** RAISE_VARARGS: arg=number of args (0 or 1 supported here). Pop exception and raise it. */
constexpr int OP_RAISE_VARARGS = 166;
/** UNPACK_EX: arg = (counts_after << 8) | counts_before. Pop sequence, push before, then a list of middle, then after. */
constexpr int OP_UNPACK_EX = 205;
/** POP_JUMP_IF_TRUE: if top is truthy, jump to arg (bytecode index). */
constexpr int OP_POP_JUMP_IF_TRUE = 204;
/** LIST_APPEND: arg=stack depth. Append TOS to list at TOS-arg. */
constexpr int OP_LIST_APPEND = 168;
/** MAP_ADD: arg=stack depth. Set key=TOS, value=TOS-1 in map at TOS-(arg+1). */
constexpr int OP_MAP_ADD = 169;
/** SET_ADD: arg=stack depth. Add TOS to set at TOS-arg. */
constexpr int OP_SET_ADD = 170;
/** BUILD_SET: pop arg values from stack, build set, push set. */
constexpr int OP_BUILD_SET = 171;
/** YIELD_VALUE: pop TOS and yield it from generator. Resume at PC+1. */
constexpr int OP_YIELD_VALUE = 172;
/** SETUP_WITH: pop context manager, push __exit__, call __enter__, push __enter__ result. Jump to arg on exception. */
constexpr int OP_SETUP_WITH = 173;
/** WITH_CLEANUP: pop __exit__ result, discard it. */
constexpr int OP_WITH_CLEANUP = 174;
/** GET_YIELD_FROM_ITER: replace TOS with iter(TOS); optimized for generator delegation. */
constexpr int OP_GET_YIELD_FROM_ITER = 175;
constexpr int OP_YIELD_FROM = 176;
/** Push a block to the stack: {PC offset of handler, stack depth}. Jump to handler on exception. */
constexpr int OP_SETUP_FINALLY = 177;
/** Pop the top block from the stack. Used when leaving try/finally/with blocks. */
constexpr int OP_POP_BLOCK = 178;
/** Pop arg values from stack, join into one string, push. */
constexpr int OP_BUILD_STRING = 179;
/** Load a variable from an enclosing scope (closure). */
constexpr int OP_LOAD_DEREF = 180;
/** Store a value into a variable in an enclosing scope (closure). */
constexpr int OP_STORE_DEREF = 181;
/** CALL_FUNCTION_EX: pop kwargs (if flag&1), then starargs, then callable. */
constexpr int OP_CALL_FUNCTION_EX = 182;
/** LIST_EXTEND: pop iterable, pop list, extend list, push list. */
constexpr int OP_LIST_EXTEND = 183;
/** DICT_UPDATE: pop iterable, pop dict, update dict, push dict. */
constexpr int OP_DICT_UPDATE = 184;
/** SET_UPDATE: pop iterable, pop set, update set, push set. */
constexpr int OP_SET_UPDATE = 185;
/** LIST_TO_TUPLE: pop list, convert to tuple, push tuple. */
constexpr int OP_LIST_TO_TUPLE = 186;
/** GET_AWAITABLE: implements `await` logic (get iterator/awaitable). */
constexpr int OP_GET_AWAITABLE = 187;
/** GET_AITER: implements `async for` iterator retrieval. */
constexpr int OP_GET_AITER = 188;
/** GET_ANEXT: implements `async for` next item retrieval. */
constexpr int OP_GET_ANEXT = 189;
/** EXCEPTION_MATCH: pop type, peek exc, push bool if isinstance(exc, type). */
constexpr int OP_EXCEPTION_MATCH = 190;
constexpr int OP_SETUP_ASYNC_WITH = 191;
constexpr int OP_BINARY_MATRIX_MULTIPLY = 192;
constexpr int OP_INPLACE_MATRIX_MULTIPLY = 193;
constexpr int OP_RERAISE = 194;
constexpr int OP_JUMP_FORWARD = 195;
constexpr int OP_FORMAT_VALUE = 196;
constexpr int OP_GEN_START = 197;
constexpr int OP_GET_LEN = 198;
constexpr int OP_MATCH_MAPPING = 199;
constexpr int OP_MATCH_SEQUENCE = 200;
constexpr int OP_EXTENDED_ARG = 201;
constexpr int OP_POP_EXCEPT = 202;
/** Pop module, copy all attributes to current frame (globals/locals). */
constexpr int OP_IMPORT_STAR = 203;

/**
 * @brief Executes a range of bytecode (one basic block). No per-instruction
 *        scheduler dispatch; runs until pc exits [pcStart, pcEnd] or RETURN_VALUE.
 *        Uses direct protoCore types (zero-copy). See REARCHITECTURE_PROTOCORE.md.
 */
struct Block {
    unsigned long handlerPc;
    size_t stackDepth;
};

const proto::ProtoObject* executeBytecodeRange(
    proto::ProtoContext* ctx,
    const proto::ProtoList* constants,
    const proto::ProtoList* bytecode,
    const proto::ProtoList* names,
    proto::ProtoObject*& frame,
    unsigned long pcStart,
    unsigned long pcEnd,
    unsigned long stackOffset = 0,
    std::vector<const proto::ProtoObject*>* externalStack = nullptr,
    unsigned long* outPc = nullptr,
    bool* yielded = nullptr,
    std::vector<Block>* externalBlockStack = nullptr);

/**
 * @brief Python-compatible generator methods.
 */
const proto::ProtoObject* py_self_iter(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs);
const proto::ProtoObject* py_generator_repr(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs);
const proto::ProtoObject* py_generator_next(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs);
const proto::ProtoObject* py_generator_send(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs);
const proto::ProtoObject* py_generator_throw(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs);
const proto::ProtoObject* py_generator_close(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs);

/**
 * @brief Executes bytecode: LOAD_CONST, RETURN_VALUE, LOAD_NAME, STORE_NAME,
 *        BINARY_ADD, BINARY_SUBTRACT, CALL_FUNCTION.
 * @param ctx Proto context.
 * @param constants List of constant values (indices 0, 1, ...).
 * @param bytecode Flat list: opcode, [arg], opcode, [arg], ...
 * @param names Optional. For LOAD_NAME/STORE_NAME: list of name strings.
 * @param frame Optional. Mutable object for LOAD_NAME/STORE_NAME (getAttribute/setAttribute).
 * @return Result of execution, or PROTO_NONE on error.
 */
const proto::ProtoObject* executeMinimalBytecode(
    proto::ProtoContext* ctx,
    const proto::ProtoList* constants,
    const proto::ProtoList* bytecode,
    const proto::ProtoList* names,
    proto::ProtoObject*& frame);

/** Invoke a Python callable with the given args list. Used by _thread bootstrap. */
const proto::ProtoObject* invokePythonCallable(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* callable,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kwargs = nullptr);

} // namespace protoPython

#endif
