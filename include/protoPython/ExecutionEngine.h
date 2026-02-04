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
    const proto::ProtoList* names = nullptr,
    proto::ProtoObject* frame = nullptr);

} // namespace protoPython

#endif
