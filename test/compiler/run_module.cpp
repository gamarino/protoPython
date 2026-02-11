#include <protoPython/PythonEnvironment.h>
#include <iostream>
#include <dlfcn.h>
#include <vector>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: run_module <path_to_so>\n";
        return 1;
    }

    // Initialize environment
    std::cout << "Initializing Python environment...\n";
    std::vector<std::string> searchPaths = { "." };
    std::vector<std::string> args = { argv[1] };
    protoPython::PythonEnvironment env("/home/gamarino/Documentos/proyectos/protoPython/lib/python3.14", searchPaths, args);
    auto* ctx = env.getContext();

    std::cout << "Initializing __main__ module...\n";
    // Ensure __main__ is initialized (Step V73)
    proto::ProtoObject* mainMod = const_cast<proto::ProtoObject*>(env.getBuiltins()->newChild(ctx, true));
    mainMod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__name__"), ctx->fromUTF8String("__main__"));
    
    protoPython::PythonEnvironment::setCurrentGlobals(mainMod);

    const proto::ProtoObject* globals = env.getGlobals();
    std::cout << "getGlobals() returned: " << globals << "\n";
    if (!globals || globals == PROTO_NONE) {
        std::cerr << "CRITICAL: globals is null or None!\n";
    }

    std::cout << "Loading shared library: " << argv[1] << "...\n";
    // Load shared library
    void* handle = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        std::cerr << "Error loading library: " << dlerror() << "\n";
        return 1;
    }

    std::cout << "Finding proto_module_init...\n";
    typedef void* (*InitFunc)();
    InitFunc init = (InitFunc)dlsym(handle, "proto_module_init");
    if (!init) {
        std::cerr << "Error finding proto_module_init: " << dlerror() << "\n";
        return 1;
    }

    std::cout << "Calling proto_module_init...\n";
    init();

    std::cout << "Cleaning up...\n";
    dlclose(handle);
    std::cout << "Done.\n";
    return 0;
}
