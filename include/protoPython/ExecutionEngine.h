#ifndef PROTOPYTHON_EXECUTIONENGINE_H
#define PROTOPYTHON_EXECUTIONENGINE_H

#include <protoCore.h>

namespace protoPython {

/** LOAD_CONST: push constants[index] onto stack. */
constexpr int OP_LOAD_CONST = 100;
/** RETURN_VALUE: pop and return top of stack. */
constexpr int OP_RETURN_VALUE = 101;

/**
 * @brief Executes minimal bytecode: LOAD_CONST and RETURN_VALUE.
 * @param ctx Proto context.
 * @param constants List of constant values (indices 0, 1, ...).
 * @param bytecode List of (opcode, arg) where opcode is OP_LOAD_CONST or OP_RETURN_VALUE.
 *                 For LOAD_CONST, arg is the constants index. For RETURN_VALUE, arg ignored.
 * @return Result of execution, or PROTO_NONE on error.
 */
const proto::ProtoObject* executeMinimalBytecode(
    proto::ProtoContext* ctx,
    const proto::ProtoList* constants,
    const proto::ProtoList* bytecode);

} // namespace protoPython

#endif
