#include <iostream>
#include <thread>
#include <protoCore.h>

int main() {
    std::cout << "[test] Initializing ProtoSpace..." << std::endl;
    proto::ProtoSpace space;
    proto::ProtoContext* ctx = space.rootContext;
    
    std::cout << "[test] Creating immutable object..." << std::endl;
    auto* obj = ctx->newObject(false);
    
    std::cout << "[test] Creating string 'foo'..." << std::endl;
    auto* name = proto::ProtoString::fromUTF8String(ctx, "foo");
    
    std::cout << "[test] Setting attribute 'foo'=42..." << std::endl;
    obj = const_cast<proto::ProtoObject*>(obj->setAttribute(ctx, name, ctx->fromInteger(42)));
    
    std::cout << "[test] Calling hasAttribute(foo)..." << std::endl;
    const proto::ProtoObject* res = obj->hasAttribute(ctx, name);
    std::cout << "[test] Result: " << (res == PROTO_TRUE ? "TRUE" : "FALSE") << std::endl;
    
    std::cout << "[test] Creating string 'bar'..." << std::endl;
    auto* name2 = proto::ProtoString::fromUTF8String(ctx, "bar");
    
    std::cout << "[test] Calling hasAttribute(bar)..." << std::endl;
    res = obj->hasAttribute(ctx, name2);
    std::cout << "[test] Result: " << (res == PROTO_TRUE ? "TRUE" : "FALSE") << std::endl;
    
    return 0;
}
