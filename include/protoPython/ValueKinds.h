#ifndef PROTOPYTHON_VALUEKINDS_H
#define PROTOPYTHON_VALUEKINDS_H

// Value-kind tests built from protoCore's public API only (headers/
// protoCore.h). protoPython never includes protoCore's private header and
// never copies its pointer tags, embedded types or cell layouts; the CTest
// case source/no-protocore-internals enforces it.
//
// Two documented facts of the public API carry the compositions below:
//  - ProtoObject::getPrototype answers, for every value that is not an
//    object cell, the ProtoSpace prototype of its kind (listPrototype,
//    unicodeCharPrototype, ...); for an object cell it answers the object's
//    first parent, or objectPrototype when it has none.
//  - ProtoObject::getFirstParent answers PROTO_NONE for every value that is
//    not an object cell, and for a parentless object.
// Several kinds' ProtoSpace prototypes are aliases of objectPrototype (see
// isAliasedCellKind), so prototype identity is used only for kinds with a
// prototype of their own (lists, Unicode characters).

#include <protoCore.h>

namespace protoPython {

// protoCore leaves the ProtoSpace prototype of several cell kinds aliased to
// objectPrototype (LargeIntegers, doubles, methods, byte buffers, threads,
// external pointers and buffers), so getPrototype cannot tell such a value
// from a parentless object. The public tag-only predicates do.
inline bool isAliasedCellKind(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return v->isDouble(ctx) || v->isInteger(ctx) || v->isMethod(ctx) || v->isByteBuffer(ctx) ||
           v->asThread(ctx) != nullptr || v->asExternalPointer(ctx) != nullptr ||
           v->asExternalBuffer(ctx) != nullptr;
}

/**
 * True when `v` is a protoCore object cell (an instance, a class, a module,
 * a function object, ...): not a SmallInteger, string, double, collection,
 * method or other tagged value. Inline rejects first; then the common case,
 * an object with a parent, is decided by one call.
 */
inline bool isObjectCell(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (!v || proto::isSmallInt(v) || proto::ProtoObject::isStringTagFast(v)) return false;
    if (v->getFirstParent(ctx) != PROTO_NONE) return true;
    if (!proto::ProtoObject::isCellPointer(v)) return false;
    const proto::ProtoObject* p = v->getPrototype(ctx);
    if (p != nullptr && p != ctx->space->objectPrototype) return false;
    return !isAliasedCellKind(ctx, v);
}

/**
 * True when `v` is a bare protoCore ProtoList (either storage form): not a
 * string, which ProtoObject::asList also answers for, and not an object
 * cell wrapping a list under `__data__`. One call for a value of another
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
