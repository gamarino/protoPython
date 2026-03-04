#include <protoPython/PythonEnvironment.h>
#include <iostream>

int main(int argc, char** argv) {
    protoPython::PythonEnvironment env("", {}, {});
    env.init(true, false);
    proto::ProtoContext* ctx = env.getContext();
    const proto::ProtoObject* mod = ctx->newObject(true);
    const proto::ProtoObject* mod2 = mod->addParent(ctx, ctx->newObject(false));
    std::cout << "mod1=" << (void*)mod << " mod2=" << (void*)mod2 << "\n";
    const proto::ProtoObject* mod3 = mod2->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "test"), ctx->newObject(false));
    std::cout << "mod2=" << (void*)mod2 << " mod3=" << (void*)mod3 << "\n";
    return 0;
}
