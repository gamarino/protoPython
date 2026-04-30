#ifndef PROTOPYTHON_DIAGUTILS_H
#define PROTOPYTHON_DIAGUTILS_H

#include <cstdlib>
#include <cstring>

namespace protoPython {
namespace {
    // Truthy helper: a diagnostic env var is considered "enabled" iff it is
    // set AND its value is not one of the explicit falsy spellings ("0",
    // "", "false", "off", "no").  Earlier revisions treated *any* set value
    // as enabled, which made the ergonomic `PROTO_ENV_DIAG=0` shorthand
    // surprisingly turn diagnostics ON.  The new rule matches the intuition
    // every other tool follows.
    inline bool envFlagEnabled(const char* name) {
        const char* v = std::getenv(name);
        if (v == nullptr) return false;
        if (v[0] == '\0') return false;
        if (std::strcmp(v, "0")     == 0) return false;
        if (std::strcmp(v, "false") == 0) return false;
        if (std::strcmp(v, "FALSE") == 0) return false;
        if (std::strcmp(v, "False") == 0) return false;
        if (std::strcmp(v, "off")   == 0) return false;
        if (std::strcmp(v, "OFF")   == 0) return false;
        if (std::strcmp(v, "no")    == 0) return false;
        if (std::strcmp(v, "NO")    == 0) return false;
        return true;
    }
    // Initialized once at program startup (before main()); no per-call guard check.
    // A function-local static would require a C++ guard check (TLS + atomic branch)
    // on every invocation, adding ~5% overhead in hot bytecode dispatch loops.
    const bool g_diag_enabled    = envFlagEnabled("PROTO_ENV_DIAG");
    // Specific diagnostic flags read on hot paths (e.g. inside
    // PythonEnvironment::getAttribute).  Caching them at startup
    // eliminates the per-call libc getenv() table walk that profiling
    // showed at ~1 % of nqueens CPU.  These are intentionally always-
    // false in production runs; flipping requires a process restart,
    // which matches how the existing `PROTO_ENV_DIAG` flag is consumed.
    const bool g_diag_dict2      = envFlagEnabled("PROTO_DICT_DIAG2");
    const bool g_diag_meta       = envFlagEnabled("PROTO_META_DIAG");
    const bool g_diag_attr       = envFlagEnabled("PROTO_ATTR_DIAG");
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
