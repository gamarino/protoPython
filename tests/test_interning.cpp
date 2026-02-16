#include <iostream>
#include <thread>
#include <protoCore.h>

int main() {
    proto::ProtoSpace space;
    proto::ProtoContext* ctx = space.rootContext;
    
    auto* s1 = proto::ProtoString::fromUTF8String(ctx, "val");
    auto* s2 = proto::ProtoString::fromUTF8String(ctx, "val");
    
    std::cout << "s1: " << (void*)s1 << std::endl;
    std::cout << "s2: " << (void*)s2 << std::endl;
    std::cout << "s1 == s2: " << (s1 == s2 ? "TRUE" : "FALSE") << std::endl;
    
    return 0;
}
