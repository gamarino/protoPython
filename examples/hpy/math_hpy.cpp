#include <protoPython/HPyABI.h>
#include <stdio.h>

/**
 * math_hpy.hpy.so
 * A simple math extension using HPy Universal ABI.
 * Step 1270.
 */

using namespace protoPython;

static HPy add_values(HPyContext* ctx, HPy self, const HPy* args, size_t nargs) {
    if (nargs < 2) return HPy_NULL;
    return HPy_Add(ctx, args[0], args[1]);
}

static HPyMethodDef MathMethods[] = {
    {"add", add_values, 0, "Adds two values"},
    {NULL, NULL, 0, NULL}
};

static HPyModuleDef moduledef = {
    "math_hpy",
    "Math extension for HPy",
    -1,
    MathMethods
};

extern "C" HPy HPyInit_math_hpy(HPyContext* ctx) {
    return HPyModule_Create(ctx, &moduledef);
}
