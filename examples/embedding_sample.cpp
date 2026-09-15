/**
 * examples/embedding_sample.cpp
 *
 * Embeds the protoPython runtime in a C++ program: runs Python source in the
 * __main__ module, reports a Python exception the way `protopy -c` does, and
 * reads a global variable back into C++.
 *
 * Built by default (CMake option PROTOPYTHON_BUILD_EXAMPLES) and run by CTest
 * as the `embedding_sample` test; the exit status is 0 only when the value read
 * back is the expected one.
 */

#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>

#include <iostream>
#include <sstream>
#include <string>

int main() {
    // The environment uses the process-wide ProtoSpace. Arguments: standard
    // library directory (empty: the environment's defaults), module search
    // paths, and sys.argv.
    protoPython::PythonEnvironment env("", {"."}, {"embedding_sample"});

    const std::string code =
        "def greet(name):\n"
        "    return f'Hello, {name} from protoPython!'\n"
        "\n"
        "result = greet('C++ Developer')\n"
        "print(result)\n";

    // Python exceptions are not C++ exceptions: a failing run returns -2 and
    // leaves the exception pending for this thread.
    if (env.executeString(code, "<embedded>") == -2) {
        const proto::ProtoObject* exc = env.takePendingException();
        if (exc && exc != PROTO_NONE) {
            std::ostringstream text;
            env.handleException(exc, nullptr, text);
            if (text.str().empty()) return env.getExitRequested();  // SystemExit
            std::cerr << text.str();
        }
        return 70;
    }

    // executeString ran the code in __main__; read the global `result` back.
    proto::ProtoContext* ctx = env.getContext();
    const proto::ProtoObject* mainModule = env.resolve("__main__", ctx);
    const proto::ProtoObject* result =
        (mainModule && mainModule != PROTO_NONE) ? env.getAttr(mainModule, "result") : nullptr;
    if (!result || !proto::ProtoObject::isStringTagFast(result)) {
        std::cerr << "embedding_sample: __main__.result is missing or not a str\n";
        return 1;
    }

    std::string text;
    result->asString(ctx)->toUTF8String(ctx, text);
    std::cout << "Retrieved result: " << text << std::endl;
    return text == "Hello, C++ Developer from protoPython!" ? 0 : 1;
}
