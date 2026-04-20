#ifndef PROTOPYTHON_DIAGUTILS_H
#define PROTOPYTHON_DIAGUTILS_H

#include <cstdlib>

namespace protoPython {

// Returns true if PROTO_ENV_DIAG is set in the environment.
// Result is cached after the first call — safe to call in hot paths.
inline bool diagEnabled() {
    static const bool val = (std::getenv("PROTO_ENV_DIAG") != nullptr);
    return val;
}

} // namespace protoPython

// Unqualified alias used throughout the implementation files.
inline bool get_env_diag() { return protoPython::diagEnabled(); }

#endif
