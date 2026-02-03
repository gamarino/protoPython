/**
 * Minimal protoCore-only test: ProtoSpace + ProtoContext, no protoPython.
 * Helps isolate whether the hang is in protoCore or protoPython.
 */
#include <protoCore.h>
#include <cstdio>

int main() {
    fprintf(stderr, "Creating ProtoSpace...\n");
    fflush(stderr);
    proto::ProtoSpace space;
    fprintf(stderr, "ProtoSpace created. Creating ProtoContext...\n");
    fflush(stderr);
    proto::ProtoContext* ctx = new proto::ProtoContext(&space);
    fprintf(stderr, "ProtoContext created. Allocating a string...\n");
    fflush(stderr);
    const proto::ProtoObject* s = ctx->fromUTF8String("hello");
    fprintf(stderr, "String created. Deleting...\n");
    fflush(stderr);
    delete ctx;
    fprintf(stderr, "OK\n");
    return 0;
}
