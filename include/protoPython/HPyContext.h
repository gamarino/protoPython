/**
 * HPyContext.h — protoPython HPy runtime shim (Phase 1).
 * Maps HPy handles to ProtoObject*. One context per thread or import session.
 * See docs/HPY_INTEGRATION_PLAN.md and docs/NEXT_20_STEPS_V63.md.
 */

#ifndef PROTOPYTHON_HPYCONTEXT_H
#define PROTOPYTHON_HPYCONTEXT_H

#include <protoCore.h>
#include <cstddef>
#include <vector>

namespace protoPython {

/** Opaque handle: index into HPyContext handle table. 0 means invalid. */
typedef unsigned long HPy;
static const HPy HPy_NULL = 0;

/** Context: handle table + ProtoContext for attribute/call. */
struct HPyContext {
    proto::ProtoContext* ctx{nullptr};
    std::vector<const proto::ProtoObject*> handles;

    explicit HPyContext(proto::ProtoContext* c) : ctx(c) {}

    /** Allocate a new handle for obj; returns 0 on failure. */
    HPy fromProtoObject(const proto::ProtoObject* obj);

    /** Resolve handle to ProtoObject*; returns nullptr if invalid. */
    const proto::ProtoObject* asProtoObject(HPy h) const;

    /** Duplicate handle (new slot pointing to same object). */
    HPy dup(HPy h);

    /** Release handle slot. */
    void close(HPy h);
};

// --- Core ABI (C-linkage style names for HPy compatibility) ---

/** Wrap ProtoObject* into an HPy handle. Caller owns the handle; must call HPy_Close when done. */
HPy HPy_FromPyObject(HPyContext* hctx, const proto::ProtoObject* obj);

/** Resolve handle to ProtoObject*. Returns nullptr if handle invalid. */
const proto::ProtoObject* HPy_AsPyObject(HPyContext* hctx, HPy h);

/** Duplicate handle (equivalent to Py_INCREF). */
HPy HPy_Dup(HPyContext* hctx, HPy h);

/** Release handle (equivalent to Py_DECREF). */
void HPy_Close(HPyContext* hctx, HPy h);

/** Get attribute by name (UTF-8). Returns new handle or 0. */
HPy HPy_GetAttr(HPyContext* hctx, HPy obj, const char* name);

/** Set attribute by name (UTF-8). Returns 0 on success, -1 on error (e.g. immutable). */
int HPy_SetAttr(HPyContext* hctx, HPy obj, const char* name, HPy value);

/** Call callable with positional args (list of handles). Returns new handle or 0. */
HPy HPy_Call(HPyContext* hctx, HPy callable, const HPy* args, size_t nargs);

/** Return type (prototype) of obj as handle. */
HPy HPy_Type(HPyContext* hctx, HPy obj);

} // namespace protoPython

#endif // PROTOPYTHON_HPYCONTEXT_H
