/*
 * BasicBlockAnalysis.h
 *
 * Analyses protoPython bytecode to compute basic block boundaries for
 * bulk task dispatch. Used by the re-architecture (REARCHITECTURE_PROTOCORE.md):
 * each block can be executed as one protoCore::Task via executeBytecodeRange.
 */

#ifndef PROTOPYTHON_BASICBLOCKANALYSIS_H
#define PROTOPYTHON_BASICBLOCKANALYSIS_H

#include <protoPython/ExecutionEngine.h>
#include <protoCore.h>
#include <vector>
#include <utility>

namespace protoPython {

/** (pcStart, pcEnd) inclusive bytecode indices for one basic block. */
using BlockBoundary = std::pair<unsigned long, unsigned long>;

/**
 * Compute basic block boundaries for the given bytecode list.
 * Bytecode is a flat list: opcode, [arg], opcode, [arg], ... (indices are
 * bytecode list indices, not instruction counts).
 *
 * Block starts: index 0 and every jump target (JUMP_ABSOLUTE, POP_JUMP_IF_FALSE, FOR_ITER).
 * Block ends: the index of an opcode that exits or jumps (RETURN_VALUE, JUMP_ABSOLUTE,
 * POP_JUMP_IF_FALSE, FOR_ITER).
 *
 * @param ctx Proto context (for reading bytecode).
 * @param bytecode Flat list of opcodes and optional args.
 * @return Vector of (start, end) inclusive boundaries; empty if bytecode is empty or invalid.
 */
std::vector<BlockBoundary> getBasicBlockBoundaries(
    proto::ProtoContext* ctx,
    const proto::ProtoList* bytecode);

} // namespace protoPython

#endif
