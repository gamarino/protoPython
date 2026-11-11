#ifndef PROTOPYTHON_BYTEARRAYTYPE_H
#define PROTOPYTHON_BYTEARRAYTYPE_H

#include <protoCore.h>

#include <string>
#include <utility>
#include <vector>

namespace protoPython {

/**
 * Builds the `bytearray` type (src/library/BytearrayType.cpp): object is its
 * only base, bytes' read-only methods serve it through wrappers, and the
 * environment records it (PythonEnvironment::getBytearrayPrototype).
 */
const proto::ProtoObject* makeBytearrayType(proto::ProtoContext* ctx, const proto::ProtoObject* objectProto,
                                            const proto::ProtoObject* typeProto,
                                            const proto::ProtoObject* bytesProto);

/**
 * Buffer exports of a bytearray, for memoryview (MemoryviewType.cpp). A
 * view is made at an epoch (bytearrayWatch); a resize advances the epoch,
 * after which reads and writes through an older view raise BufferError. A
 * held export (bytearrayHold, +1 on `with`, -1 on release) makes a resize
 * raise BufferError, as CPython's exports do. Each call publishes or reads
 * one version of the content: the export word and the bytes travel together.
 * All return false when `ba` is not a bytearray (or after raising).
 */
bool bytearrayWatch(proto::ProtoContext* ctx, const proto::ProtoObject* ba, long long& epoch,
                    size_t* sizeOut = nullptr);
bool bytearrayHold(proto::ProtoContext* ctx, const proto::ProtoObject* ba, long long epoch, int delta);
bool bytearrayViewRead(proto::ProtoContext* ctx, const proto::ProtoObject* ba, long long epoch, std::string& out);
/** The bytes [from, to) of one version (clipped to its size), in O(to - from). */
bool bytearrayViewReadRange(proto::ProtoContext* ctx, const proto::ProtoObject* ba, long long epoch,
                            size_t from, size_t to, std::string& out);
bool bytearrayViewWrite(proto::ProtoContext* ctx, const proto::ProtoObject* ba, long long epoch,
                        const std::vector<std::pair<size_t, std::string>>& pieces);
/** Raises BufferError with `msg` (never returns). */
[[noreturn]] void raiseBufferError(proto::ProtoContext* ctx, const std::string& msg);

} // namespace protoPython

#endif
