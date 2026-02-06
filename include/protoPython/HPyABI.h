#ifndef PROTOPYTHON_HPYABI_H
#define PROTOPYTHON_HPYABI_H

/**
 * HPyABI.h — Minimal HPy Universal ABI definitions for protoPython.
 * This file defines the structure of HPyContext and core types that 
 * Universal ABI modules expect to find via their include path.
 * Step 1208.
 */

#include <protoPython/HPyContext.h>

namespace protoPython {

#define HPy_ABI_VERSION 1

// In a real HPy implementation, this would match HPy's hpy.h exactly.
// For Phase 2, we provide enough to compile simple extensions.

/** 
 * HPyModuleDef: Module metadata for HPy. 
 * Usually passed as a static struct in the init function.
 */
struct HPyModuleDef {
    const char* m_name;
    const char* m_doc;
    std::ptrdiff_t m_size;
    void* m_methods; // HPyMethodDef* in full impl
    // ... slot, traverse, clear, free ...
};

/**
 * HPyMethodDef and Function pointers.
 */
typedef HPy (*HPyCFunction)(HPyContext* ctx, HPy self, const HPy* args, size_t nargs);

struct HPyMethodDef {
    const char* ml_name;
    HPyCFunction ml_meth;
    int ml_flags;
    const char* ml_doc;
};

/**
 * Type Creation (Step 1255)
 */
struct HPyType_Slot {
    int slot;
    void* pfunc;
};

struct HPyType_Spec {
    const char* name;
    int basicsize;
    int itemsize;
    unsigned int flags;
    HPyType_Slot* slots;
};

// Slot constants (Simplified)
#define HPy_tp_methods 1
#define HPy_tp_doc 2
#define HPy_tp_traverse 3
#define HPy_tp_finalize 4

} // namespace protoPython

#endif // PROTOPYTHON_HPYABI_H
