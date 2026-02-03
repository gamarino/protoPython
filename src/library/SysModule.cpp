#include <protoPython/SysModule.h>

namespace protoPython {
namespace sys {

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* sys = ctx->newObject(true);
    
    // sys.platform
#if defined(_WIN32)
    const char* plat = "win32";
#elif defined(__APPLE__)
    const char* plat = "darwin";
#else
    const char* plat = "linux";
#endif
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "platform"), ctx->fromUTF8String(plat));
    
    // sys.version
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "version"), ctx->fromUTF8String("3.14.0 (protoPython, Feb 2026)"));

    // sys.path (empty for now, PythonEnvironment will populate it)
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "path"), ctx->newList()->asObject(ctx));

    // sys.modules (dict mapping names to modules)
    sys = sys->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "modules"), ctx->newObject(true));

    return sys;
}

} // namespace sys
} // namespace protoPython
