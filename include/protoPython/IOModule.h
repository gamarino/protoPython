#ifndef PROTOPYTHON_IOMODULE_H
#define PROTOPYTHON_IOMODULE_H

#include <protoCore.h>
#include <streambuf>

namespace protoPython {
namespace io {

const proto::ProtoObject* initialize(proto::ProtoContext* ctx);

/** sys.stdin: a text file object over descriptor 0, with a binary `buffer`
 *  (see IOModule.cpp). `ioModule` is the object initialize() returned. */
const proto::ProtoObject* makeStandardInput(proto::ProtoContext* ctx, const proto::ProtoObject* ioModule);

/** A stream buffer over standard input that shares sys.stdin's read-ahead
 *  buffer: protopy installs it as std::cin's, so the REPL, input() and
 *  sys.stdin read one stream in order (ReadConsoleW on a Windows console). */
std::streambuf* standardInputBuffer();

} // namespace io
} // namespace protoPython

#endif // PROTOPYTHON_IOMODULE_H
