#include <protoPython/PathlibModule.h>
#include <string>

namespace protoPython {
namespace pathlib {

static const proto::ProtoString* path_data_name(proto::ProtoContext* ctx) {
    return proto::ProtoString::fromUTF8String(ctx, "__data__");
}

/** Path.__call__(*parts) -> new Path instance; stores path string in __data__. */
static const proto::ProtoObject* py_path_call(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    std::string path = ".";
    if (posArgs && posArgs->getSize(ctx) >= 1) {
        const proto::ProtoObject* first = posArgs->getAt(ctx, 0);
        if (first->isString(ctx)) {
            first->asString(ctx)->toUTF8String(ctx, path);
        }
        for (unsigned long i = 1; i < posArgs->getSize(ctx); ++i) {
            path += "/";
            const proto::ProtoObject* part = posArgs->getAt(ctx, static_cast<int>(i));
            if (part->isString(ctx)) {
                std::string s;
                part->asString(ctx)->toUTF8String(ctx, s);
                path += s;
            }
        }
    }
    const proto::ProtoObject* pathProto = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__path_proto__"));
    if (!pathProto) return PROTO_NONE;
    const proto::ProtoObject* p = pathProto->newChild(ctx, true);
    p->setAttribute(ctx, path_data_name(ctx), ctx->fromUTF8String(path.c_str()));
    return p;
}

/** Path.__str__ -> return __data__ string. */
static const proto::ProtoObject* py_path_str(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    const proto::ProtoObject* data = self->getAttribute(ctx, path_data_name(ctx));
    if (data && data->isString(ctx)) return data;
    return ctx->fromUTF8String(".");
}

static const proto::ProtoObject* py_path_exists(
    proto::ProtoContext*,
    const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    return PROTO_FALSE;
}

static const proto::ProtoObject* py_path_is_dir(
    proto::ProtoContext*,
    const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    return PROTO_FALSE;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* pathProto = ctx->newObject(true);
    pathProto = pathProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__path_proto__"), pathProto);
    pathProto = pathProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__call__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(pathProto), py_path_call));
    pathProto = pathProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__str__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(pathProto), py_path_str));
    pathProto = pathProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "exists"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(pathProto), py_path_exists));
    pathProto = pathProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "is_dir"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(pathProto), py_path_is_dir));

    const proto::ProtoObject* mod = ctx->newObject(true);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "Path"), pathProto);
    return mod;
}

} // namespace pathlib
} // namespace protoPython
