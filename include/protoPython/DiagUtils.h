#ifndef PROTOPYTHON_DIAGUTILS_H
#define PROTOPYTHON_DIAGUTILS_H

#include <cstdlib>

namespace protoPython {
namespace {
    // Initialized once at program startup (before main()); no per-call guard check.
    // A function-local static would require a C++ guard check (TLS + atomic branch)
    // on every invocation, adding ~5% overhead in hot bytecode dispatch loops.
    const bool g_diag_enabled = (std::getenv("PROTO_ENV_DIAG") != nullptr);
}

inline bool diagEnabled() { return g_diag_enabled; }

} // namespace protoPython

// Unqualified alias used throughout the implementation files.
inline bool get_env_diag() { return protoPython::diagEnabled(); }

#endif
