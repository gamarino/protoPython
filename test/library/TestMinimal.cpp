/**
 * Minimal reproducer for FoundationTest hang - no GTest.
 * Requires STDLIB_PATH compile definition (as TestFoundation).
 * Bisects: use protoCore directly first, then full PythonEnvironment.
 */
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>
#include <cstdio>
#include <cstdlib>

#ifndef STDLIB_PATH
#define STDLIB_PATH ""
#endif

int main() {
    fprintf(stderr, "A: ProtoSpace+Context (no Python)...\n");
    fflush(stderr);
    {
        proto::ProtoSpace space;
        proto::ProtoContext* ctx = new proto::ProtoContext(&space);
        for (int i = 0; i < 200; i++) (void)ctx->fromUTF8String("x");
        delete ctx;
    }
    fprintf(stderr, "B: ProtoSpace+Context OK. Creating PythonEnvironment...\n");
    fflush(stderr);
    protoPython::PythonEnvironment env(STDLIB_PATH);
    fprintf(stderr, "C: PythonEnvironment created.\n");
    fflush(stderr);

    auto* obj = env.getObjectPrototype();
    auto* typ = env.getTypePrototype();
    auto* lst = env.getListPrototype();
    if (!obj || !typ || !lst) {
        fprintf(stderr, "FAIL: null prototype\n");
        return 1;
    }
    fprintf(stderr, "OK: BasicTypesExist\n");
    return 0;
}
