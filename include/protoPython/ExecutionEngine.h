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
