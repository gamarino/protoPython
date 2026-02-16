#include <protoPython/OsPathModule.h>
#include <protoCore.h>
#include <string>

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#endif

namespace protoPython {
namespace os_path {

static const proto::ProtoObject* py_join(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    (void)self;
    if (!posArgs || posArgs->getSize(ctx) == 0)
        return ctx->fromUTF8String("");
    std::string out;
    const char* sep = "/";
    if (posArgs->getSize(ctx) == 1) {
        const proto::ProtoObject* iterable = posArgs->getAt(ctx, 0);
        const proto::ProtoObject* iterM = iterable->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"));
        if (!iterM || !iterM->asMethod(ctx)) return ctx->fromUTF8String("");
        const proto::ProtoObject* it = iterM->asMethod(ctx)(ctx, iterable, nullptr, ctx->newList(), nullptr);
        if (!it) return ctx->fromUTF8String("");
        const proto::ProtoObject* nextM = it->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__"));
        if (!nextM || !nextM->asMethod(ctx)) return ctx->fromUTF8String("");
        bool first = true;
        for (;;) {
            const proto::ProtoObject* part = nextM->asMethod(ctx)(ctx, it, nullptr, ctx->newList(), nullptr);
            if (!part || part == PROTO_NONE) break;
            if (!first) out += sep;
            first = false;
            if (part->isString(ctx)) {
                std::string s;
                part->asString(ctx)->toUTF8String(ctx, s);
                out += s;
            }
        }
    } else {
        for (unsigned long i = 0; i < posArgs->getSize(ctx); ++i) {
            if (i > 0) out += sep;
            const proto::ProtoObject* part = posArgs->getAt(ctx, static_cast<int>(i));
            if (part->isString(ctx)) {
                std::string s;
                part->asString(ctx)->toUTF8String(ctx, s);
                out += s;
            }
        }
    }
    return ctx->fromUTF8String(out.c_str());
}

static const proto::ProtoObject* py_exists(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    (void)self;
    if (!posArgs || posArgs->getSize(ctx) < 1) return PROTO_FALSE;
    const proto::ProtoObject* pathObj = posArgs->getAt(ctx, 0);
    if (!pathObj->isString(ctx)) return PROTO_FALSE;
    std::string path;
    pathObj->asString(ctx)->toUTF8String(ctx, path);
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    struct stat st;
    return (stat(path.c_str(), &st) == 0) ? PROTO_TRUE : PROTO_FALSE;
#else
    (void)path;
    return PROTO_FALSE;
#endif
}

static const proto::ProtoObject* py_isfile(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    (void)self;
    if (!posArgs || posArgs->getSize(ctx) < 1) return PROTO_FALSE;
    const proto::ProtoObject* pathObj = posArgs->getAt(ctx, 0);
    if (!pathObj->isString(ctx)) return PROTO_FALSE;
    std::string path;
    pathObj->asString(ctx)->toUTF8String(ctx, path);
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return PROTO_FALSE;
    return S_ISREG(st.st_mode) ? PROTO_TRUE : PROTO_FALSE;
#else
    (void)path;
    return PROTO_FALSE;
#endif
}

static const proto::ProtoObject* py_isdir(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    (void)self;
    if (!posArgs || posArgs->getSize(ctx) < 1) return PROTO_FALSE;
    const proto::ProtoObject* pathObj = posArgs->getAt(ctx, 0);
    if (!pathObj->isString(ctx)) return PROTO_FALSE;
    std::string path;
    pathObj->asString(ctx)->toUTF8String(ctx, path);
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return PROTO_FALSE;
    return S_ISDIR(st.st_mode) ? PROTO_TRUE : PROTO_FALSE;
#else
    (void)path;
    return PROTO_FALSE;
#endif
}

static const proto::ProtoObject* py_realpath(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    (void)self;
    if (!posArgs || posArgs->getSize(ctx) < 1) return ctx->fromUTF8String("");
    const proto::ProtoObject* path = posArgs->getAt(ctx, 0);
    return path->isString(ctx) ? path : ctx->fromUTF8String("");
}

static const proto::ProtoObject* py_normpath(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    (void)self;
    if (!posArgs || posArgs->getSize(ctx) < 1) return ctx->fromUTF8String("");
    const proto::ProtoObject* path = posArgs->getAt(ctx, 0);
    return path->isString(ctx) ? path : ctx->fromUTF8String("");
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(true);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "join"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_join));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "exists"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_exists));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "isfile"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_isfile));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "isdir"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_isdir));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "realpath"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_realpath));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "normpath"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_normpath));
    return mod;
}

} // namespace os_path
} // namespace protoPython
