/**
 * A program that embeds protoPython keeps its own process-wide C runtime
 * invalid-parameter handler (Windows): the library makes invalid arguments
 * (a closed descriptor, ...) errors only on the threads that run Python code,
 * and restores the creating thread's handler when the environment goes away.
 * On every platform, closing an invalid descriptor from Python raises OSError
 * instead of ending the process.
 */
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

#if defined(_WIN32)
static void hostHandler(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {}
#endif

static int fail(const std::string& why) {
    std::cerr << "invalid_parameter_scope: " << why << "\n";
    return 1;
}

int main() {
#if defined(_WIN32)
    _set_invalid_parameter_handler(hostHandler);
#endif
    {
        protoPython::PythonEnvironment env(STDLIB_PATH, {"."}, {"invalid_parameter_scope"});
#if defined(_WIN32)
        if (_get_invalid_parameter_handler() != hostHandler)
            return fail("the library replaced the host's process-wide handler");
        if (_get_thread_local_invalid_parameter_handler() == nullptr)
            return fail("the thread that runs Python code reports no invalid arguments");
#endif
        const std::string code =
            "import os\n"
            "try:\n"
            "    os.close(987654)\n"
            "except OSError as e:\n"
            "    closed = e.errno\n"
            "else:\n"
            "    closed = None\n";
        if (env.executeString(code, "<embedded>") == -2) {
            const proto::ProtoObject* exc = env.takePendingException();
            std::ostringstream text;
            if (exc && exc != PROTO_NONE) env.handleException(exc, nullptr, text);
            return fail("the Python code raised: " + text.str());
        }
        proto::ProtoContext* ctx = env.getContext();
        const proto::ProtoObject* mainModule = env.resolve("__main__", ctx);
        const proto::ProtoObject* closed = mainModule ? env.getAttr(mainModule, "closed") : nullptr;
        if (!closed || !closed->isInteger(ctx)) return fail("os.close of a bad descriptor did not raise OSError");
    }
#if defined(_WIN32)
    if (_get_thread_local_invalid_parameter_handler() != nullptr)
        return fail("the creating thread's handler was not restored");
    if (_get_invalid_parameter_handler() != hostHandler)
        return fail("the host's process-wide handler changed");
#endif
    std::cout << "invalid_parameter_scope: ok\n";
    return 0;
}
