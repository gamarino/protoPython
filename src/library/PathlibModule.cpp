#include <protoPython/PathlibModule.h>
#include <protoCore.h>
#include <string>

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#include <sys/types.h>
#endif

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

static std::string path_from_self(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    const proto::ProtoObject* data = self->getAttribute(ctx, path_data_name(ctx));
    if (!data || !data->isString(ctx)) return ".";
    std::string s;
    data->asString(ctx)->toUTF8String(ctx, s);
    return s;
}

static const proto::ProtoObject* py_path_exists(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    std::string path = path_from_self(ctx, self);
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    struct stat st;
    return (stat(path.c_str(), &st) == 0) ? PROTO_TRUE : PROTO_FALSE;
#else
    (void)path;
    return PROTO_FALSE;
#endif
}

static const proto::ProtoObject* py_path_is_dir(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    std::string path = path_from_self(ctx, self);
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return PROTO_FALSE;
    return S_ISDIR(st.st_mode) ? PROTO_TRUE : PROTO_FALSE;
#else
    (void)path;
    return PROTO_FALSE;
#endif
}

static const proto::ProtoObject* py_path_is_file(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    std::string path = path_from_self(ctx, self);
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return PROTO_FALSE;
    return S_ISREG(st.st_mode) ? PROTO_TRUE : PROTO_FALSE;
#else
    (void)path;
    return PROTO_FALSE;
#endif
}

static const proto::ProtoObject* py_path_mkdir(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    std::string path = path_from_self(ctx, self);
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    mode_t mode = 0755;
    if (posArgs && posArgs->getSize(ctx) >= 1) {
        const proto::ProtoObject* m = posArgs->getAt(ctx, 0);
        if (m->isInteger(ctx))
            mode = static_cast<mode_t>(m->asLong(ctx) & 07777);
    }
    (void)mkdir(path.c_str(), mode);
#endif
    (void)path;
    return PROTO_NONE;
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
    pathProto = pathProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "is_file"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(pathProto), py_path_is_file));
    pathProto = pathProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "mkdir"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(pathProto), py_path_mkdir));

    const proto::ProtoObject* mod = ctx->newObject(true);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "Path"), pathProto);
    return mod;
}

} // namespace pathlib
} // namespace protoPython
