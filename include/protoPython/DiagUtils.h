#ifndef PROTOPYTHON_DIAGUTILS_H
#define PROTOPYTHON_DIAGUTILS_H

#include <cstdlib>
#include <cstring>

// PROTOPY_DIAG_ENABLED controls whether the diagnostic accessors below are
// runtime-checkable (1) or compile-time false (0). Default: ON in debug
// builds, OFF in release (NDEBUG) so the optimiser dead-code-eliminates
// every `if (diagXxxEnabled()) { ... }` branch. Override at configure
// time with `-DPROTOPY_DIAG_ENABLED=1` if you need to chase a problem
// in a release-shaped build.
//
// Motivation (2026-06-15): perf profiling showed `diagEnabled` self-time
// at 0.5-2.1 % across every microbench because the per-opcode
// `if (diagEnabled()) { ... }` was paying for a memory load + branch on
// a value that never changes once the process starts. With this gate,
// the entire diagnostic-checking apparatus compiles away in release.
#ifndef PROTOPY_DIAG_ENABLED
#  ifdef NDEBUG
#    define PROTOPY_DIAG_ENABLED 0
#  else
#    define PROTOPY_DIAG_ENABLED 1
#  endif
#endif

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
    //
    // POLICY (2026-05-24): no `std::getenv(...)` may appear on any code path
    // that runs more than once per process — call it at TU init from this
    // file, cache as a `const bool`, expose via an inline accessor.  A
    // per-opcode getenv hammered libc's strcmp table walk for 6.4 % of
    // binary_trees CPU before this audit landed.
    const bool g_diag_enabled    = envFlagEnabled("PROTO_ENV_DIAG");
    const bool g_diag_dict2      = envFlagEnabled("PROTO_DICT_DIAG2");
    const bool g_diag_meta       = envFlagEnabled("PROTO_META_DIAG");
    const bool g_diag_attr       = envFlagEnabled("PROTO_ATTR_DIAG");
    // Diagnostic flags consumed across the runtime (PythonEnvironment.cpp,
    // ThreadModule.cpp, BuiltinsModule.cpp, PythonModuleProvider.cpp,
    // ExecutionEngine.cpp).  Each one was, prior to the May 2026 audit,
    // a fresh `std::getenv(...)` per call site — sometimes per opcode.
    const bool g_diag_thread     = envFlagEnabled("PROTO_THREAD_DIAG");
    const bool g_diag_resolve    = envFlagEnabled("PROTO_RESOLVE_DIAG");
    const bool g_diag_import     = envFlagEnabled("PROTO_IMPORT_DIAG");
    const bool g_diag_mod        = envFlagEnabled("PROTO_MOD_DIAG");
    const bool g_diag_mod_update = envFlagEnabled("PROTO_MOD_UPDATE_DIAG");
    const bool g_diag_env_debug  = envFlagEnabled("PROTO_ENV_DEBUG");
    const bool g_diag_repl       = envFlagEnabled("PROTO_REPL_DIAG");
    const bool g_diag_class_tr   = envFlagEnabled("PROTO_CLASS_TRACE");
    const bool g_diag_set        = envFlagEnabled("PROTOPY_SET_DBG");
    const bool g_diag_hpy        = envFlagEnabled("PROTO_HPY_DEBUG");
    const bool g_diag_pc_trace   = envFlagEnabled("PROTO_PC_TRACE");
    const bool g_diag_subscr     = envFlagEnabled("PROTO_SUBSCR_DIAG");
    // Behavioural flags (not pure diagnostics — control real runtime decisions).
    const bool g_quiet_warnings  = envFlagEnabled("PROTOPY_QUIET_WARNINGS")
                                    || envFlagEnabled("PYTHONDONTWRITEWARNINGS");
    const bool g_no_color        = envFlagEnabled("NO_COLOR");
}

#if PROTOPY_DIAG_ENABLED
inline bool diagEnabled()         { return g_diag_enabled; }
inline bool diagDict2Enabled()    { return g_diag_dict2; }
inline bool diagMetaEnabled()     { return g_diag_meta; }
inline bool diagAttrEnabled()     { return g_diag_attr; }
inline bool diagThreadEnabled()   { return g_diag_thread; }
inline bool diagResolveEnabled()  { return g_diag_resolve; }
inline bool diagImportEnabled()   { return g_diag_import; }
inline bool diagModEnabled()      { return g_diag_mod; }
inline bool diagModUpdateEnabled(){ return g_diag_mod_update; }
inline bool diagEnvDebugEnabled() { return g_diag_env_debug; }
inline bool diagReplEnabled()     { return g_diag_repl; }
inline bool diagClassTraceEnabled(){return g_diag_class_tr; }
inline bool diagSetEnabled()      { return g_diag_set; }
inline bool diagHpyEnabled()      { return g_diag_hpy; }
inline bool diagPcTraceEnabled()  { return g_diag_pc_trace; }
inline bool diagSubscrEnabled()   { return g_diag_subscr; }
#else
// Release-mode constants. The optimiser dead-code-eliminates every
// `if (diagXxxEnabled()) { … }` block, including the function calls
// inside it, so the diagnostic apparatus has zero runtime cost.
constexpr bool diagEnabled()         { return false; }
constexpr bool diagDict2Enabled()    { return false; }
constexpr bool diagMetaEnabled()     { return false; }
constexpr bool diagAttrEnabled()     { return false; }
constexpr bool diagThreadEnabled()   { return false; }
constexpr bool diagResolveEnabled()  { return false; }
constexpr bool diagImportEnabled()   { return false; }
constexpr bool diagModEnabled()      { return false; }
constexpr bool diagModUpdateEnabled(){ return false; }
constexpr bool diagEnvDebugEnabled() { return false; }
constexpr bool diagReplEnabled()     { return false; }
constexpr bool diagClassTraceEnabled(){return false; }
constexpr bool diagSetEnabled()      { return false; }
constexpr bool diagHpyEnabled()      { return false; }
constexpr bool diagPcTraceEnabled()  { return false; }
constexpr bool diagSubscrEnabled()   { return false; }
#endif

// Behavioural flags — NOT diagnostics. These control real runtime
// decisions (warning suppression, NO_COLOR) so they always have to
// honour the env-var configuration even in release.
inline bool quietWarnings()       { return g_quiet_warnings; }
inline bool noColor()             { return g_no_color; }

} // namespace protoPython

// Unqualified aliases used throughout the implementation files.
inline bool get_env_diag()       { return protoPython::diagEnabled(); }
inline bool get_env_dict2_diag() { return protoPython::diagDict2Enabled(); }
inline bool get_env_meta_diag()  { return protoPython::diagMetaEnabled(); }
inline bool get_env_attr_diag()  { return protoPython::diagAttrEnabled(); }

#endif
