#ifndef PROTOPYTHON_DAPSERVER_H
#define PROTOPYTHON_DAPSERVER_H

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace protoPython {

/**
 * @brief The Debug Adapter Protocol server behind `protopy --dap`.
 *
 * The protocol runs over the process's stdin/stdout (Content-Length framed
 * JSON, as VS Code speaks it). The script's own standard output and error are
 * captured and delivered as `output` events, and its standard input reads end of
 * file. The script runs on the calling thread, through `runner`, once the client
 * sent `launch` and `configurationDone`; the line events of that thread
 * (include/protoPython/Debugger.h) drive breakpoints, stepping and inspection.
 *
 * See docs/debugging.md for the requests supported and the limitations.
 */
class DAPServer {
public:
    static bool isSupported() { return true; }

    /** What the client's `launch` request asked for. */
    struct LaunchRequest {
        std::string program;                 ///< the script, absolute
        std::vector<std::string> args;       ///< sys.argv[1:]
        std::string cwd;                     ///< empty: unchanged
        std::vector<std::pair<std::string, std::string>> env;
    };
    /** Runs the program as `protopy <program> <args>` would and returns its exit status. */
    using ProgramRunner = std::function<int(const LaunchRequest&)>;

    /** Serves one debug session; returns the process's exit status. */
    static int run(const ProgramRunner& runner);
};

} // namespace protoPython

#endif
