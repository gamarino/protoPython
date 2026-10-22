// protopy_umd_host.cpp -- runs a Python script with a native UMD provider registered.
//
// protoPython imports modules through protoCore's Unified Module Discovery
// (ProtoSpace::getImportModule over the space's resolution chain).  A module
// written in C++ -- a ModuleProvider compiled into a shared library -- is
// reachable from Python once its provider is registered with protoCore's
// ProviderRegistry and named in the chain; `import example.geometry` then
// loads the module registered under the logical path "example/geometry"
// (docs/EXCEPTIONS.md, "Importing a UMD module").  protopy ships no plug-in
// loader, so the test suite uses this host: it links protoCore's example
// geometry module (protoCore/examples/exceptions/, shared library
// protocore_example_geometry), registers its provider, appends
// "provider:example_geometry" to the chain and runs the script as protopy
// would.
//
//     protopy_umd_host [-p <dir>]... <script.py>
//
// -p adds a module search directory (a protopyc-compiled module, for the
// compiled-code variant of the test).  An exception the script does not
// handle is reported the Python way on stderr and the exit status is 1.

#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>

#include "geometry_module/GeometryModule.h"

#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string directoryOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

// Appends `entry` to the space's resolution chain: Python's own providers
// keep their precedence.
void appendToResolutionChain(proto::ProtoContext* ctx, const char* entry) {
    proto::ProtoSpace* space = ctx->space;
    const proto::ProtoObject* chainObj = space->getResolutionChain();
    const proto::ProtoList* chain = (chainObj && chainObj != PROTO_NONE) ? chainObj->asList(ctx) : nullptr;
    if (!chain) chain = ctx->newList();
    chain = chain->appendLast(ctx, ctx->fromUTF8String(entry));
    space->setResolutionChain(chain->asObject(ctx));
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> searchPaths;
    std::string script;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            searchPaths.push_back(argv[++i]);
        } else if (script.empty()) {
            script = argv[i];
        } else {
            std::fprintf(stderr, "usage: %s [-p <dir>]... <script.py>\n", argv[0]);
            return 64;
        }
    }
    if (script.empty()) {
        std::fprintf(stderr, "usage: %s [-p <dir>]... <script.py>\n", argv[0]);
        return 64;
    }
    std::ifstream in(script, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "file not found: %s\n", script.c_str());
        return 66;
    }
    const std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    searchPaths.insert(searchPaths.begin(), directoryOf(script));

    // The provider is process-global; the chain entry belongs to the space.
    example::registerGeometryProvider();
    int status = 0;
    try {
        protoPython::PythonEnvironment env(STDLIB_PATH, searchPaths, {script});
        appendToResolutionChain(env.getContext(), "provider:example_geometry");
        // An uncaught exception is reported while `env` -- whose space holds
        // the exception's value and its pin -- is alive.
        try {
            env.executeString(source, script);
        } catch (const proto::ProtoException& e) {
            std::ostringstream report;
            env.handleException(e.value(), nullptr, report);
            std::cerr << report.str() << std::flush;
            status = report.str().empty() ? env.getExitRequested() : 1;
        }
        env.runExitHandlers();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "protopy_umd_host: unhandled C++ exception: %s\n", e.what());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "protopy_umd_host: an exception of an unknown C++ type reached the top level\n");
        return 1;
    }
    return status;
}
