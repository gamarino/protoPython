#ifndef PROTOPYTHON_COLLECTIONSABCMODULE_H
#define PROTOPYTHON_COLLECTIONSABCMODULE_H

#include <protoCore.h>

namespace protoPython {
namespace collections_abc {

const proto::ProtoObject* initialize(proto::ProtoContext* ctx);

/**
 * True when `cls` is a virtual subclass of `abc`, one of the native ABCs of
 * this module: a type registered with abc.register() (CPython's registrations
 * of the built-in types included), a virtual subclass of one of the ABCs
 * derived from `abc`, or, for the one-method ABCs (Iterable, Sized, ...), a
 * type that defines the methods.  False for any other `abc`; issubclass and
 * isinstance call it only after the ordinary MRO check failed.
 */
bool isVirtualSubclass(proto::ProtoContext* ctx, const proto::ProtoObject* cls,
                       const proto::ProtoObject* abc);

} // namespace collections_abc
} // namespace protoPython

#endif
