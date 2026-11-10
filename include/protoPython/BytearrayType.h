#ifndef PROTOPYTHON_BYTEARRAYTYPE_H
#define PROTOPYTHON_BYTEARRAYTYPE_H

#include <protoCore.h>

namespace protoPython {

/**
 * Builds the `bytearray` type (src/library/BytearrayType.cpp): object is its
 * only base, bytes' read-only methods serve it through wrappers, and the
 * environment records it (PythonEnvironment::getBytearrayPrototype).
 */
const proto::ProtoObject* makeBytearrayType(proto::ProtoContext* ctx, const proto::ProtoObject* objectProto,
                                            const proto::ProtoObject* typeProto,
                                            const proto::ProtoObject* bytesProto);

} // namespace protoPython

#endif
