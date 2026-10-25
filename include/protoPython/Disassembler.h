#ifndef PROTOPYTHON_DISASSEMBLER_H
#define PROTOPYTHON_DISASSEMBLER_H

#include <protoCore.h>
#include <iosfwd>
#include <string>

namespace protoPython {

/**
 * The bytecode disassembler behind `protopy --disasm` (docs/VM.md).
 *
 * A listing shows a code object (its header, constants, names, variable
 * slots and instructions) and then, in order of appearance, every code
 * object held in its constants (functions, lambdas, class bodies,
 * comprehensions), nested ones included.  The output holds no addresses or
 * other run-dependent values, so it can be compared with golden files
 * (docs/vm-examples).
 */

/** The opcode's name without the `OP_` prefix, or nullptr when `op` is not
 *  an opcode of include/protoPython/ExecutionEngine.h. */
const char* opcodeName(int op);

/** Writes the listing of `code` and of every code object in its constants. */
void disassemble(proto::ProtoContext* ctx, const proto::ProtoObject* code, std::ostream& out);

/**
 * Compiles the Python source file `path` as `protopy` compiles a module (the
 * peephole specialiser included) without running it, and writes the listing
 * of the module's code object.  Answers 0, or 1 after writing the syntax or
 * compile error to `err`.
 */
int disassembleFile(proto::ProtoContext* ctx, const std::string& path, std::ostream& out, std::ostream& err);

} // namespace protoPython

#endif
