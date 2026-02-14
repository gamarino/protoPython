/**
 * examples/embedding_sample.cpp
 * 
 * This example demonstrates how to embed the protoPython runtime into a C++ application.
 * It initializes the PythonEnvironment, sets up a context, and executes a script.
 */

#include <protoPython/PythonEnvironment.h>
#include <protoPython/ExecutionEngine.h>
#include <protoCore.h>
#include <iostream>
#include <vector>

int main() {
    // 1. Initialize the protoCore Space
    // A Space is the root of memory and execution for the process.
    proto::ProtoSpace space;

    // 2. Initialize the PythonEnvironment
    // The environment manages the Python runtime state, builtins, and modules.
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::initialize(&space);

    // 3. Obtain a Context for the current thread
    // All protoCore operations must occur within a active Context.
    proto::ProtoContext* ctx = space.getContext();

    // 4. Create a Root Scope for execution
    // This allows us to share objects between the C++ host and Python.
    const proto::ProtoObject* globals = ctx->newObject(true);
    if (env->getObjectPrototype()) {
        globals = globals->addParent(ctx, env->getObjectPrototype());
    }

    // 5. Prepare a Python Script
    std::string code = 
        "def greet(name):\n"
        "    return f'Hello, {name} from protoPython!'\n"
        "\n"
        "result = greet('C++ Developer')\n"
        "print(result)\n";

    std::cout << "--- Executing Embedded Python ---" << std::endl;

    try {
        // 6. Execute the code
        // We use the PythonEnvironment's executeModule or executeBytecodeRange.
        env->executeModule(ctx, "main", code, globals);

        // 7. Retrieve results from globals
        const proto::ProtoString* resultKey = proto::ProtoString::fromUTF8String(ctx, "result");
        const proto::ProtoObject* resultObj = globals->getAttribute(ctx, resultKey);

        if (resultObj && resultObj->isString(ctx)) {
            std::cout << "Retrieved Result: " << resultObj->asString(ctx)->toUTF8String() << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "Execution failed: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "--- Execution Completed ---" << std::endl;

    return 0;
}
