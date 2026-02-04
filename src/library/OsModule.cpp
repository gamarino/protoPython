#include <protoPython/OsModule.h>
#include <protoCore.h>
#include <cstdlib>
#include <string>

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace protoPython {
namespace os_module {

static const proto::ProtoObject* py_getenv(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* keyObj = posArgs->getAt(ctx, 0);
    if (!keyObj->isString(ctx)) return PROTO_NONE;
    std::string key;
    keyObj->asString(ctx)->toUTF8String(ctx, key);
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    const char* val = std::getenv(key.c_str());
    if (val) return ctx->fromUTF8String(val);
#endif
    return PROTO_NONE;
}

static const proto::ProtoObject* py_getcwd(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    char buf[4096];
    if (getcwd(buf, sizeof(buf)))
        return ctx->fromUTF8String(buf);
#endif
    return ctx->fromUTF8String(".");
}

static const proto::ProtoObject* py_chdir(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* pathObj = posArgs->getAt(ctx, 0);
    if (!pathObj->isString(ctx)) return PROTO_NONE;
    std::string path;
    pathObj->asString(ctx)->toUTF8String(ctx, path);
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    if (chdir(path.c_str()) == 0)
        return PROTO_NONE;
#endif
    return PROTO_NONE;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(true);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "getenv"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_getenv));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "getcwd"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_getcwd));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "chdir"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_chdir));
    return mod;
}

} // namespace os_module
} // namespace protoPython
