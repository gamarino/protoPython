#ifndef PROTOPYTHON_DIAGUTILS_H
#define PROTOPYTHON_DIAGUTILS_H

#include <cstdlib>

namespace protoPython {
namespace {
    // Initialized once at program startup (before main()); no per-call guard check.
    // A function-local static would require a C++ guard check (TLS + atomic branch)
    // on every invocation, adding ~5% overhead in hot bytecode dispatch loops.
    const bool g_diag_enabled    = (std::getenv("PROTO_ENV_DIAG")    != nullptr);
    // Specific diagnostic flags read on hot paths (e.g. inside
    // PythonEnvironment::getAttribute).  Caching them at startup
    // eliminates the per-call libc getenv() table walk that profiling
    // showed at ~1 % of nqueens CPU.  These are intentionally always-
    // false in production runs; flipping requires a process restart,
    // which matches how the existing `PROTO_ENV_DIAG` flag is consumed.
    const bool g_diag_dict2      = (std::getenv("PROTO_DICT_DIAG2")  != nullptr);
    const bool g_diag_meta       = (std::getenv("PROTO_META_DIAG")   != nullptr);
    const bool g_diag_attr       = (std::getenv("PROTO_ATTR_DIAG")   != nullptr);
}

inline bool diagEnabled()       { return g_diag_enabled; }
inline bool diagDict2Enabled()  { return g_diag_dict2; }
inline bool diagMetaEnabled()   { return g_diag_meta; }
inline bool diagAttrEnabled()   { return g_diag_attr; }

} // namespace protoPython

// Unqualified aliases used throughout the implementation files.
inline bool get_env_diag()       { return protoPython::diagEnabled(); }
inline bool get_env_dict2_diag() { return protoPython::diagDict2Enabled(); }
inline bool get_env_meta_diag()  { return protoPython::diagMetaEnabled(); }
inline bool get_env_attr_diag()  { return protoPython::diagAttrEnabled(); }

#endif
