#ifndef PROTOPYTHON_VALUEKINDS_H
#define PROTOPYTHON_VALUEKINDS_H

// Value-kind tests built from protoCore's public API only (headers/
// protoCore.h). protoPython never includes protoCore's private header and
// never copies its pointer tags, embedded types or cell layouts; the CTest
// case source/no-protocore-internals enforces it.
//
// protoPython does not ask whether a value is a "plain object cell": protoCore
// resolves attributes on every value through its prototype, and
// getFirstParent, hasOwnAttribute, getOwnAttributeDirect, hasParent and
// isInstanceOf answer every value, so the runtime asks the type question
// (or does the lookup) directly.  What remains here are kind tests for the
// one question the type cannot answer: which protoCore collection a
// `__data__` payload is (the wrapper-vs-bare duality of lists, dicts and
// sets).  They rest on one documented fact of the public API:
// ProtoObject::getPrototype answers, for a value that is not an object, the
// ProtoSpace prototype of its kind (listPrototype, sparseListPrototype,
// unicodeCharPrototype, ...), and for an object its first parent;
// getFirstParent answers PROTO_NONE for a value without parents.

#include <protoCore.h>

namespace protoPython {

/**
 * True when `v` is a bare protoCore ProtoList (either storage form): not a
 * string, which ProtoObject::asList also answers for, and not an object
 * wrapping a list under `__data__`. One call for a value of another
 * kind; SmallIntegers are rejected inline.
 */
inline bool isProtoList(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (!v || proto::isSmallInt(v) || v->getPrototype(ctx) != ctx->space->listPrototype) return false;
    return v->getFirstParent(ctx) == PROTO_NONE;
}

/**
 * True when `v` is a bare protoCore ProtoMap (a dict's collision bucket): the
 * same composition as isProtoList, because ProtoObject::asMap also answers the
 * map an object cell keeps under `__data__`.
 */
inline bool isProtoMap(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (!v || proto::isSmallInt(v) || v->getPrototype(ctx) != ctx->space->mapPrototype) return false;
    return v->getFirstParent(ctx) == PROTO_NONE;
}

/**
 * True when `v` is a bare protoCore ProtoSparseList (a dict's or a set's
 * storage): the same composition as isProtoList, because
 * ProtoObject::asSparseList also answers the storage an object keeps under
 * `__data__`.
 */
inline bool isProtoSparseList(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (!v || proto::isSmallInt(v) || v->getPrototype(ctx) != ctx->space->sparseListPrototype) return false;
    return v->getFirstParent(ctx) == PROTO_NONE;
}

/**
 * True when `v` is a protoCore Unicode character (ProtoContext::
 * fromUnicodeChar). protoCore has no public isUnicodeChar; a Unicode
 * character is the one value that is not a cell and whose prototype is
 * ProtoSpace::unicodeCharPrototype. Its code point is `v->asLong(ctx)`.
 */
inline bool isUnicodeChar(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return v && !proto::isSmallInt(v) && !proto::ProtoObject::isCellPointer(v) &&
           v->getPrototype(ctx) == ctx->space->unicodeCharPrototype;
}

} // namespace protoPython

#endif
