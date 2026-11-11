#ifndef PROTOPYTHON_MEMORYVIEWTYPE_H
#define PROTOPYTHON_MEMORYVIEWTYPE_H

#include <protoCore.h>

#include <string>

namespace protoPython {

/**
 * Builds the `memoryview` type (src/library/MemoryviewType.cpp) and records
 * it in the environment (PythonEnvironment::getMemoryviewPrototype).
 */
const proto::ProtoObject* makeMemoryviewType(proto::ProtoContext* ctx, const proto::ProtoObject* objectProto,
                                             const proto::ProtoObject* typeProto);

/** Whether `obj` is a memoryview (an instance of the type, recognised by type). */
bool isMemoryview(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

/**
 * The bytes a memoryview views, in its item order, read from one version of
 * the viewed object: false when `obj` is not a memoryview; raises ValueError
 * for a released view and BufferError for a view of a bytearray resized
 * since the view was made.
 */
bool memoryviewContent(proto::ProtoContext* ctx, const proto::ProtoObject* obj, std::string& out);

/** Writes `data` at byte offset `at` of the view's content, in one publish. */
bool memoryviewWrite(proto::ProtoContext* ctx, const proto::ProtoObject* obj, size_t at, const std::string& data);

/**
 * `owner.__release_buffer__(view)`: releases `view`, a view of `owner`'s
 * bytes that `owner.__buffer__` returned. Raises TypeError for anything but
 * a memoryview and ValueError for a released view or a view of another
 * object, as CPython does.
 */
const proto::ProtoObject* memoryviewReleaseBuffer(proto::ProtoContext* ctx, const proto::ProtoObject* owner,
                                                  const proto::ProtoObject* view);

/** The object a memoryview exports (its `obj`), or nullptr. */
const proto::ProtoObject* memoryviewExporter(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

} // namespace protoPython

#endif
