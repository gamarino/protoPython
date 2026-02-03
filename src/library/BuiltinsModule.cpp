#include <protoPython/BuiltinsModule.h>

namespace protoPython {
namespace builtins {

const proto::ProtoObject* initialize(proto::ProtoContext* ctx, const proto::ProtoObject* objectProto, 
                                   const proto::ProtoObject* typeProto, const proto::ProtoObject* intProto,
                                   const proto::ProtoObject* strProto, const proto::ProtoObject* listProto,
                                   const proto::ProtoObject* dictProto) {
    const proto::ProtoObject* builtins = ctx->newObject(true);
    
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "object"), objectProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "type"), typeProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "int"), intProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "str"), strProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "list"), listProto);
    builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "dict"), dictProto);
    
    // Add some functions
    // builtins = builtins->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "print"), ...);

    return builtins;
}

} // namespace builtins
} // namespace protoPython
