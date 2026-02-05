/*
 * BasicBlockAnalysis.cpp
 *
 * Computes basic block boundaries from protoPython bytecode for bulk
 * task scheduling. See docs/REARCHITECTURE_PROTOCORE.md.
 */

#include <protoPython/BasicBlockAnalysis.h>
#include <algorithm>
#include <unordered_set>

namespace protoPython {

static bool opHasArg(int op) {
    return (op == OP_LOAD_CONST || op == OP_LOAD_NAME || op == OP_STORE_NAME ||
            op == OP_LOAD_FAST || op == OP_STORE_FAST ||
            op == OP_CALL_FUNCTION || op == OP_LOAD_ATTR || op == OP_STORE_ATTR ||
            op == OP_BUILD_LIST || op == OP_BUILD_MAP || op == OP_BUILD_TUPLE ||
            op == OP_UNPACK_SEQUENCE || op == OP_LOAD_GLOBAL || op == OP_STORE_GLOBAL ||
            op == OP_BUILD_SLICE || op == OP_FOR_ITER || op == OP_POP_JUMP_IF_FALSE ||
            op == OP_JUMP_ABSOLUTE || op == OP_COMPARE_OP || op == OP_BINARY_SUBSCR ||
            op == OP_STORE_SUBSCR);
}

static bool opIsBlockEnd(int op) {
    return op == OP_RETURN_VALUE || op == OP_JUMP_ABSOLUTE ||
           op == OP_POP_JUMP_IF_FALSE || op == OP_FOR_ITER;
}

static int getOpAt(proto::ProtoContext* ctx, const proto::ProtoList* bytecode, unsigned long pc) {
    const proto::ProtoObject* obj = bytecode->getAt(ctx, static_cast<int>(pc));
    return (obj && obj->isInteger(ctx)) ? static_cast<int>(obj->asLong(ctx)) : -1;
}

static long getArgAt(proto::ProtoContext* ctx, const proto::ProtoList* bytecode, unsigned long pc) {
    if (pc + 1 >= bytecode->getSize(ctx)) return -1;
    const proto::ProtoObject* obj = bytecode->getAt(ctx, static_cast<int>(pc + 1));
    return (obj && obj->isInteger(ctx)) ? static_cast<long>(obj->asLong(ctx)) : -1;
}

std::vector<BlockBoundary> getBasicBlockBoundaries(
    proto::ProtoContext* ctx,
    const proto::ProtoList* bytecode) {
    std::vector<BlockBoundary> out;
    if (!ctx || !bytecode) return out;
    unsigned long n = bytecode->getSize(ctx);
    if (n == 0) return out;

    std::unordered_set<unsigned long> blockStarts;
    blockStarts.insert(0);

    for (unsigned long pc = 0; pc < n; ) {
        int op = getOpAt(ctx, bytecode, pc);
        if (op < 0) break;
        if (opIsBlockEnd(op)) {
            long arg = getArgAt(ctx, bytecode, pc);
            if (op != OP_RETURN_VALUE && arg >= 0 && static_cast<unsigned long>(arg) < n)
                blockStarts.insert(static_cast<unsigned long>(arg));
        }
        pc += opHasArg(op) ? 2 : 1;
    }

    std::vector<unsigned long> starts(blockStarts.begin(), blockStarts.end());
    std::sort(starts.begin(), starts.end());

    for (size_t i = 0; i < starts.size(); ++i) {
        unsigned long pcStart = starts[i];
        unsigned long pcEnd = pcStart;
        unsigned long pc = pcStart;
        while (pc < n) {
            int op = getOpAt(ctx, bytecode, pc);
            if (op < 0) break;
            pcEnd = pc;
            if (opIsBlockEnd(op)) break;
            pc += opHasArg(op) ? 2 : 1;
        }
        if (pcEnd >= pcStart)
            out.push_back({pcStart, pcEnd});
    }
    return out;
}

} // namespace protoPython
