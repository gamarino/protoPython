#ifndef PROTOPYTHON_STRUCTSEQUENCE_H
#define PROTOPYTHON_STRUCTSEQUENCE_H

#include <protoCore.h>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace protoPython {

/** One field of a struct sequence. A null name marks an unnamed field, which
 *  is reachable by index only (like the integer st_atime slot of
 *  os.stat_result). */
struct StructSequenceField {
    const char* name;
    const proto::ProtoObject* value;
};

/**
 * Builds a CPython-style struct sequence: an object that behaves as a tuple of
 * its first nVisible field values (indexing, slicing, len, iteration,
 * comparison, hashing, unpacking, isinstance(x, tuple)) and also exposes every
 * named field, visible or not, as an attribute.
 *
 * The object has the tuple prototype as its parent, __class__ = tuple and a
 * ProtoTuple in __data__, so all tuple methods apply unchanged. Unlike CPython,
 * type(x) is tuple and repr(x) is the plain tuple repr.
 *
 * Fields beyond nVisible are attribute-only. nVisible larger than the number of
 * fields makes every field visible.
 */
const proto::ProtoObject* newStructSequence(
    proto::ProtoContext* ctx,
    const std::vector<StructSequenceField>& fields,
    size_t nVisible = SIZE_MAX);

} // namespace protoPython

#endif // PROTOPYTHON_STRUCTSEQUENCE_H
