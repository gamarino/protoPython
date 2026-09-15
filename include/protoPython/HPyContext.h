/**
 * HPyContext.h — protoPython HPy runtime shim (Phase 1).
 * Maps HPy handles to ProtoObject*. One context per thread or import session.
 * See docs/HPY_INTEGRATION_PLAN.md and docs/NEXT_20_STEPS_V63.md.
 */

#ifndef PROTOPYTHON_HPYCONTEXT_H
#define PROTOPYTHON_HPYCONTEXT_H

#include <protoCore.h>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace protoPython {

/** Opaque handle: index into HPyContext handle table. 0 means invalid. */
typedef unsigned long HPy;
static const HPy HPy_NULL = 0;

/**
 * Context: handle table + ProtoContext for attribute/call.
 *
 * Handle lifetime and garbage collection:
 * - Every open handle pins its object in a proto::ProtoRootSet owned by this
 *   context (created with ProtoSpace::createRootSet("hpy-handles")), so the
 *   collector keeps the object alive from handle creation until HPy_Close or
 *   until the context is destroyed. The destructor destroys the root set, which
 *   releases the handles still open.
 * - The loader creates one context per HPyInit_<name> call and the method
 *   wrapper one per HPy method call. A native function must resolve the handle
 *   it returns to a ProtoObject* before its context is destroyed; the caller
 *   then keeps the value reachable (operand stack, module attribute).
 * - Objects that must outlive the call (for example, handed to a thread or a
 *   callback) must be pinned by the extension in its own ProtoRootSet;
 *   HPyField and HPyGlobal are not implemented.
 * - A context is used by one thread at a time. Handle numbers are recycled
 *   after HPy_Close; closing an already closed handle has no effect.
 */
struct HPyContext {
    proto::ProtoContext* ctx{nullptr};
    /** Space that owns `roots`; captured at construction. */
    proto::ProtoSpace* space{nullptr};
    /** Pins the objects of all open handles; nullptr only without a context. */
    proto::ProtoRootSet* roots{nullptr};
    /** Slot i holds the root-set handle of HPy i + 1; kNullHandle marks a free slot. */
    std::vector<proto::ProtoRootSet::Handle> handles;
    std::vector<size_t> freeList; // Step 1211: recycling for scalability
    /** Interned attribute names; interned strings are process-perpetual, so they need no pin. */
    std::map<std::string, const proto::ProtoString*> stringCache; // Step 1289: interning

    explicit HPyContext(proto::ProtoContext* c);
    ~HPyContext();
    HPyContext(const HPyContext&) = delete;
    HPyContext& operator=(const HPyContext&) = delete;

    /** Allocate a new handle for obj and pin obj; returns 0 on failure. */
    HPy fromProtoObject(const proto::ProtoObject* obj);

    /** Resolve handle to ProtoObject*; returns nullptr if invalid or closed. */
    const proto::ProtoObject* asProtoObject(HPy h) const;

    /** Duplicate handle (new slot and new pin for the same object). */
    HPy dup(HPy h);

    /** Release the handle's pin and recycle its slot. */
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

/** Create a module from HPyModuleDef. (Step 1214) */
struct HPyModuleDef; // Forward decl
HPy HPyModule_Create(HPyContext* hctx, HPyModuleDef* def);

// --- Phase 3: API Coverage (v65) ---

/** Object creation (Step 1225-1227) */
HPy HPy_New(HPyContext* hctx, HPy type, void** data);
HPy HPy_FromLong(HPyContext* hctx, long long v);
HPy HPy_FromDouble(HPyContext* hctx, double v);
HPy HPy_FromUTF8(HPyContext* hctx, const char* utf8);

/** Numeric/String access (Step 1228-1229) */
long long HPy_AsLong(HPyContext* hctx, HPy h);
double HPy_AsDouble(HPyContext* hctx, HPy h);
const char* HPy_AsUTF8(HPyContext* hctx, HPy h); // Returns temporary pointer valid until hande closed

/** Exceptions (Step 1230-1232) */
void HPyErr_SetString(HPyContext* hctx, HPy type, const char* msg);
HPy HPyErr_NewException(HPyContext* hctx, const char* name, HPy base, HPy dict);
int HPyErr_Occurred(HPyContext* hctx);
void HPyErr_Clear(HPyContext* hctx);

/** Protocols (Step 1233-1238) */
HPy HPy_Add(HPyContext* hctx, HPy h1, HPy h2);
HPy HPy_Sub(HPyContext* hctx, HPy h1, HPy h2);
HPy HPy_Mul(HPyContext* hctx, HPy h1, HPy h2);
HPy HPy_Div(HPyContext* hctx, HPy h1, HPy h2);
HPy HPy_GetItem(HPyContext* hctx, HPy obj, HPy key);
int HPy_SetItem(HPyContext* hctx, HPy obj, HPy key, HPy value);
size_t HPy_Length(HPyContext* hctx, HPy obj);
int HPy_Contains(HPyContext* hctx, HPy container, HPy item);
int HPy_IsTrue(HPyContext* hctx, HPy h);

/** Static Attribute Access (Step 1239) */
HPy HPy_GetAttr_s(HPyContext* hctx, HPy obj, const char* name);
int HPy_SetAttr_s(HPyContext* hctx, HPy obj, const char* name, HPy value);

/** Helpers (Step 1240) */
int HPyModule_AddObject(HPyContext* hctx, HPy mod, const char* name, HPy obj);

// --- Phase 3.2: Advanced API (v66) ---

/** Bitwise Protocols (Step 1245) */
HPy HPy_And(HPyContext* hctx, HPy h1, HPy h2);
HPy HPy_Or(HPyContext* hctx, HPy h1, HPy h2);
HPy HPy_Xor(HPyContext* hctx, HPy h1, HPy h2);
HPy HPy_LShift(HPyContext* hctx, HPy h1, HPy h2);
HPy HPy_RShift(HPyContext* hctx, HPy h1, HPy h2);

/** Rich Comparison (Step 1246) */
enum HPy_ComparisonOp { HPy_LT = 0, HPy_LE = 1, HPy_EQ = 2, HPy_NE = 3, HPy_GT = 4, HPy_GE = 5 };
HPy HPy_RichCompare(HPyContext* hctx, HPy h1, HPy h2, int op);

/** Collections: List (Step 1247, 1250) */
HPy HPyList_New(HPyContext* hctx, size_t len);
int HPyList_Append(HPyContext* hctx, HPy list, HPy item);

/** Collections: Dict (Step 1248) */
HPy HPyDict_New(HPyContext* hctx);
int HPyDict_SetItem(HPyContext* hctx, HPy dict, HPy key, HPy value);
HPy HPyDict_GetItem(HPyContext* hctx, HPy dict, HPy key);

/** Collections: Tuple (Step 1249, 1251) */
HPy HPyTuple_New(HPyContext* hctx, size_t len);
HPy HPyTuple_Pack(HPyContext* hctx, size_t n, ...);

/** Iterators (Step 1252) */
HPy HPy_GetIter(HPyContext* hctx, HPy obj);
HPy HPy_Next(HPyContext* hctx, HPy iter);

/** Slices (Step 1253) */
HPy HPySlice_New(HPyContext* hctx, HPy start, HPy stop, HPy step);

/** Type Creation (Step 1255-1256) */
struct HPyType_Spec; // Forward decl
HPy HPyType_FromSpec(HPyContext* hctx, HPyType_Spec* spec);

/** Metadata & Constants (Step 1257-1260) */
int HPyModule_AddStringConstant(HPyContext* hctx, HPy mod, const char* name, const char* value);
int HPyModule_AddIntConstant(HPyContext* hctx, HPy mod, const char* name, long long value);
HPy HPy_CallMethod(HPyContext* hctx, HPy obj, const char* name, const HPy* args, size_t nargs);

/** Debugging (Step 1293) */
void HPy_Dump(HPyContext* hctx, HPy h);

/** Memory & Refcount Stubs (Step 1261) */
static inline void HPy_Incref(HPyContext* hctx, HPy h) {} // protoCore is GC-based
static inline void HPy_Decref(HPyContext* hctx, HPy h) {}

// --- Method Wrapper (Step 1213) ---

typedef HPy (*HPyCFunction)(HPyContext* ctx, HPy self, const HPy* args, size_t nargs);

/** 
 * Create a ProtoObject that acts as a wrapper for an HPyCFunction.
 * The function pointer is stored in an attribute (e.g. __hpy_meth__).
 */
const proto::ProtoObject* HPy_WrapMethod(HPyContext* hctx, const char* name, HPyCFunction meth);

} // namespace protoPython

#endif // PROTOPYTHON_HPYCONTEXT_H
