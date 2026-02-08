#include <protoPython/OsModule.h>
#include <protoCore.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <iostream>

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
extern char** environ;
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
    for (char** p = environ; p && *p; ++p) {
        const char* eq = std::strchr(*p, '=');
        if (eq && eq > *p) {
            size_t klen = static_cast<size_t>(eq - *p);
            if (klen == key.size() && std::strncmp(*p, key.c_str(), klen) == 0) {
                return ctx->fromUTF8String(eq + 1);
            }
        }
    }
#endif
    if (posArgs->getSize(ctx) >= 2)
        return posArgs->getAt(ctx, 1);
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

static const proto::ProtoObject* py_listdir(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    std::string path = ".";
    if (posArgs->getSize(ctx) >= 1) {
        const proto::ProtoObject* pathObj = posArgs->getAt(ctx, 0);
        if (pathObj->isString(ctx))
            pathObj->asString(ctx)->toUTF8String(ctx, path);
    }
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    const proto::ProtoList* result = ctx->newList();
    DIR* d = opendir(path.c_str());
    if (!d) return result->asObject(ctx);
    for (;;) {
        struct dirent* e = readdir(d);
        if (!e) break;
        const char* n = e->d_name;
        if (n[0] == '.' && (n[1] == '\0' || (n[1] == '.' && n[2] == '\0')))
            continue;
        result = result->appendLast(ctx, ctx->fromUTF8String(n));
    }
    closedir(d);
    return result->asObject(ctx);
#else
    (void)path;
    return ctx->newList()->asObject(ctx);
#endif
}

static const proto::ProtoObject* py_remove(
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
    (void)unlink(path.c_str());
#endif
    return PROTO_NONE;
}

static const proto::ProtoObject* py_rmdir(
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
    (void)rmdir(path.c_str());
#endif
    return PROTO_NONE;
}

static const proto::ProtoObject* py_setenv(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* keyObj = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* valObj = posArgs->getAt(ctx, 1);
    if (!keyObj->isString(ctx) || !valObj->isString(ctx)) return PROTO_NONE;
    std::string key, val;
    keyObj->asString(ctx)->toUTF8String(ctx, key);
    valObj->asString(ctx)->toUTF8String(ctx, val);
    if (std::getenv("PROTO_ENV_DIAG")) std::cerr << "[proto-os] setenv key=" << key << " val=" << val << "\n";
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    setenv(key.c_str(), val.c_str(), 1);
#endif
    return PROTO_NONE;
}

static const proto::ProtoObject* py_unsetenv(
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
    unsetenv(key.c_str());
#endif
    return PROTO_NONE;
}

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#endif

static const proto::ProtoObject* py_waitpid(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    int pid = static_cast<int>(posArgs->getAt(ctx, 0)->asLong(ctx));
    int options = static_cast<int>(posArgs->getAt(ctx, 1)->asLong(ctx));
    int status = 0;
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    int res = waitpid(pid, &status, options);
    const proto::ProtoList* tuple = ctx->newList();
    tuple = tuple->appendLast(ctx, ctx->fromInteger(res));
    tuple = tuple->appendLast(ctx, ctx->fromInteger(status));
    return ctx->newTupleFromList(tuple)->asObject(ctx);
#else
    return PROTO_NONE;
#endif
}

static const proto::ProtoObject* py_kill(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    int pid = static_cast<int>(posArgs->getAt(ctx, 0)->asLong(ctx));
    int sig = static_cast<int>(posArgs->getAt(ctx, 1)->asLong(ctx));
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    kill(pid, sig);
#endif
    return PROTO_NONE;
}

static const proto::ProtoObject* py_pipe(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    int pipefds[2];
    if (pipe(pipefds) == 0) {
        const proto::ProtoList* tuple = ctx->newList();
        tuple = tuple->appendLast(ctx, ctx->fromInteger(pipefds[0]));
        tuple = tuple->appendLast(ctx, ctx->fromInteger(pipefds[1]));
        return ctx->newTupleFromList(tuple)->asObject(ctx);
    }
#endif
    return PROTO_NONE;
}

static const proto::ProtoObject* py_environ_keys(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    const proto::ProtoList* result = ctx->newList();
    for (char** p = environ; p && *p; ++p) {
        const char* eq = strchr(*p, '=');
        if (eq && eq > *p) {
            std::string key(*p, static_cast<size_t>(eq - *p));
            result = result->appendLast(ctx, ctx->fromUTF8String(key.c_str()));
        }
    }
    return result->asObject(ctx);
#else
    return ctx->newList()->asObject(ctx);
#endif
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(true);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "getenv"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_getenv));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "getcwd"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_getcwd));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "chdir"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_chdir));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "listdir"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_listdir));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "remove"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_remove));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "rmdir"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_rmdir));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "environ_keys"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_environ_keys));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "putenv"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_setenv));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "unsetenv"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_unsetenv));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "waitpid"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_waitpid));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "kill"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_kill));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "pipe"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_pipe));
    return mod;
}

} // namespace os_module
} // namespace protoPython
