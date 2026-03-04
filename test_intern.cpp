#include <iostream>
#include <protoCore.h>
int main(int argc, char** argv) {
    proto::ProtoContext* ctx = proto::initialize(argc, argv);
    const proto::ProtoString* s1 = ctx->fromUTF8String("AVeryLongStringIndeed");
    const proto::ProtoString* s2 = ctx->fromUTF8String("AVeryLongStringIndeed");
    if (s1 == s2) std::cout << "INTERNED\n"; else std::cout << "NOT INTERNED\n";
    return 0;
}
