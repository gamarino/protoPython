/**
 * Minimal reproducer for FoundationTest hang - no GTest.
 * Requires STDLIB_PATH compile definition (as TestFoundation).
 * Used for debugging; see docs/TESTING.md.
 */
#include <protoPython/PythonEnvironment.h>
#include <cstdio>
#include <cstdlib>

#ifndef STDLIB_PATH
#define STDLIB_PATH ""
#endif

int main() {
    fprintf(stderr, "Creating PythonEnvironment...\n");
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
