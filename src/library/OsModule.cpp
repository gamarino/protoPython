#include <protoPython/OsModule.h>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <iostream>

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
extern char** environ;
#endif

namespace protoPython {
namespace os_module {

struct ScandirState {
    DIR* dir;
    std::string path;
    bool exhausted;

    ScandirState(DIR* d, const std::string& p) : dir(d), path(p), exhausted(false) {}
    ~ScandirState() {
        if (dir) closedir(dir);
    }
};

static void scandir_finalizer(void* ptr) {
    delete static_cast<ScandirState*>(ptr);
}

static const proto::ProtoObject* direntry_proto = nullptr;

static const proto::ProtoObject* py_direntry_is_dir(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
    const proto::ProtoObject* pathObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "path"));
    if (!pathObj || !pathObj->isString(ctx)) return PROTO_FALSE;
    std::string path;
    pathObj->asString(ctx)->toUTF8String(ctx, path);
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode) ? PROTO_TRUE : PROTO_FALSE;
    }
    return PROTO_FALSE;
}

static const proto::ProtoObject* py_direntry_is_file(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
    const proto::ProtoObject* pathObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "path"));
    if (!pathObj || !pathObj->isString(ctx)) return PROTO_FALSE;
    std::string path;
    pathObj->asString(ctx)->toUTF8String(ctx, path);
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return S_ISREG(st.st_mode) ? PROTO_TRUE : PROTO_FALSE;
    }
    return PROTO_FALSE;
}

static const proto::ProtoObject* py_direntry_is_symlink(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
    const proto::ProtoObject* pathObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "path"));
    if (!pathObj || !pathObj->isString(ctx)) return PROTO_FALSE;
    std::string path;
    pathObj->asString(ctx)->toUTF8String(ctx, path);
    struct stat st;
    if (lstat(path.c_str(), &st) == 0) {
        return S_ISLNK(st.st_mode) ? PROTO_TRUE : PROTO_FALSE;
    }
    return PROTO_FALSE;
}

static const proto::ProtoObject* py_direntry_fspath(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
    const proto::ProtoObject* path = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "path"));
    if (path) return path;
    // If called on the prototype itself, or if path is missing, return None to avoid polluting fspath logic
    return PROTO_NONE;
}

static const proto::ProtoObject* py_scandir_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
    const proto::ProtoObject* stateObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__state__"));
    if (!stateObj || !stateObj->asExternalPointer(ctx)) return nullptr;
    ScandirState* state = static_cast<ScandirState*>(stateObj->asExternalPointer(ctx)->getPointer(ctx));
    if (!state || state->exhausted) return nullptr;

    for (;;) {
        struct dirent* e = readdir(state->dir);
        if (!e) {
            state->exhausted = true;
            return nullptr;
        }
        const char* n = e->d_name;
        if (n[0] == '.' && (n[1] == '\0' || (n[1] == '.' && n[2] == '\0')))
            continue;

        const proto::ProtoObject* entry = ctx->newObject(true);
        if (direntry_proto) entry = entry->addParent(ctx, direntry_proto);
        
        std::string fullPath = state->path;
        if (fullPath.back() != '/') fullPath += "/";
        fullPath += n;

        entry = entry->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "name"), ctx->fromUTF8String(n));
        entry = entry->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "path"), ctx->fromUTF8String(fullPath.c_str()));
        return entry;
    }
}

static const proto::ProtoObject* py_scandir_iter(
    proto::ProtoContext* /*ctx*/,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
    return self;
}

static const proto::ProtoObject* py_stat_result_getitem(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    long long idx = posArgs->getAt(ctx, 0)->asLong(ctx);
    const char* keys[] = {
        "st_mode", "st_ino", "st_dev", "st_nlink", "st_uid", "st_gid", "st_size", "st_atime", "st_mtime", "st_ctime"
    };
    if (idx >= 0 && idx < 10) {
        return self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, keys[idx]));
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* make_stat_result(proto::ProtoContext* ctx, const struct stat& st) {
    const proto::ProtoObject* res = ctx->newObject(true);
    res = res->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "st_mode"), ctx->fromInteger(st.st_mode));
    res = res->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "st_ino"), ctx->fromInteger(st.st_ino));
    res = res->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "st_dev"), ctx->fromInteger(st.st_dev));
    res = res->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "st_nlink"), ctx->fromInteger(st.st_nlink));
    res = res->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "st_uid"), ctx->fromInteger(st.st_uid));
    res = res->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "st_gid"), ctx->fromInteger(st.st_gid));
    res = res->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "st_size"), ctx->fromInteger(st.st_size));
    res = res->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "st_atime"), ctx->fromInteger(st.st_atime));
    res = res->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "st_mtime"), ctx->fromInteger(st.st_mtime));
    res = res->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "st_ctime"), ctx->fromInteger(st.st_ctime));
    
    // Add __getitem__ for indexing support
    res = res->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__getitem__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(res), py_stat_result_getitem));
    
    return res;
}

static const proto::ProtoObject* py_direntry_stat(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
    const proto::ProtoObject* pathObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "path"));
    if (!pathObj || !pathObj->isString(ctx)) return PROTO_NONE;
    std::string path;
    pathObj->asString(ctx)->toUTF8String(ctx, path);
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return make_stat_result(ctx, st);
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_direntry_inode(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
    const proto::ProtoObject* pathObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "path"));
    if (!pathObj || !pathObj->isString(ctx)) return PROTO_NONE;
    std::string path;
    pathObj->asString(ctx)->toUTF8String(ctx, path);
    struct stat st;
    if (lstat(path.c_str(), &st) == 0) {
        return ctx->fromInteger(st.st_ino);
    }
    return PROTO_NONE;
}

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
    if (std::getenv("PROTO_ENV_DIAG")) {
        // log removed
    }
    const char* val = std::getenv(key.c_str());
    if (val) return ctx->fromUTF8String(val);
    if (posArgs->getSize(ctx) >= 2) return posArgs->getAt(ctx, 1);
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

static const proto::ProtoObject* py_scandir(
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

    DIR* d = opendir(path.c_str());
    if (!d) return PROTO_NONE;

    ScandirState* state = new ScandirState(d, path);
    const proto::ProtoObject* iter = ctx->newObject(true);
    iter = iter->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__state__"),
        ctx->fromExternalPointer(state, scandir_finalizer));
    iter = iter->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(iter), py_scandir_next));
    iter = iter->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(iter), py_scandir_iter));
    
    return iter;
}

static const proto::ProtoObject* py_stat(
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
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return make_stat_result(ctx, st);
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_lstat(
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
    struct stat st;
    if (lstat(path.c_str(), &st) == 0) {
        return make_stat_result(ctx, st);
    }
    return PROTO_NONE;
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
    if (unlink(path.c_str()) != 0) {
        // Handle error?
    }
#endif
    return PROTO_NONE;
}

static const proto::ProtoObject* py_unlink(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwargs) {
    return py_remove(ctx, self, parentLink, posArgs, kwargs);
}

static const proto::ProtoObject* py_mkdir(
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
    int mode = 0777;
    if (posArgs->getSize(ctx) >= 2) mode = static_cast<int>(posArgs->getAt(ctx, 1)->asLong(ctx));
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    (void)mkdir(path.c_str(), mode);
#endif
    return PROTO_NONE;
}

static const proto::ProtoObject* py_rename(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    std::string oldPath, newPath;
    posArgs->getAt(ctx, 0)->asString(ctx)->toUTF8String(ctx, oldPath);
    posArgs->getAt(ctx, 1)->asString(ctx)->toUTF8String(ctx, newPath);
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    (void)rename(oldPath.c_str(), newPath.c_str());
#endif
    return PROTO_NONE;
}

static const proto::ProtoObject* py_replace(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwargs) {
    return py_rename(ctx, self, parentLink, posArgs, kwargs);
}

static const proto::ProtoObject* py_access(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 2) return PROTO_FALSE;
    std::string path;
    posArgs->getAt(ctx, 0)->asString(ctx)->toUTF8String(ctx, path);
    int mode = static_cast<int>(posArgs->getAt(ctx, 1)->asLong(ctx));
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    return access(path.c_str(), mode) == 0 ? PROTO_TRUE : PROTO_FALSE;
#else
    return PROTO_FALSE;
#endif
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
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    if (std::getenv("PROTO_ENV_DIAG")) {
        // log removed
    }
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
    if (std::getenv("PROTO_ENV_DIAG")) {
        // log removed
    }
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
    // log removed
    const proto::ProtoList* result = ctx->newList();
    for (char** p = environ; p && *p; ++p) {
        const char* eq = strchr(*p, '=');
        if (eq && eq > *p) {
            std::string key(*p, static_cast<size_t>(eq - *p));
            if (std::getenv("PROTO_ENV_DIAG")) {
                // log removed
            }
            result = result->appendLast(ctx, ctx->fromUTF8String(key.c_str()));
        }
    }
    // log removed
    return result->asObject(ctx);
#else
    return ctx->newList()->asObject(ctx);
#endif
}

static const proto::ProtoObject* py_environ_getitem(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    return py_getenv(ctx, nullptr, nullptr, posArgs, nullptr);
}

static const proto::ProtoObject* py_environ_setitem(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    return py_setenv(ctx, nullptr, nullptr, posArgs, nullptr);
}

static const proto::ProtoObject* py_environ_delitem(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    return py_unsetenv(ctx, nullptr, nullptr, posArgs, nullptr);
}

static const proto::ProtoObject* py_environ_keys_method(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
    return py_environ_keys(ctx, nullptr, nullptr, nullptr, nullptr);
}

static const proto::ProtoObject* py_getuid(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    return ctx->fromInteger(getuid());
#else
    return ctx->fromInteger(0);
#endif
}

static const proto::ProtoObject* py_geteuid(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    return ctx->fromInteger(geteuid());
#else
    return ctx->fromInteger(0);
#endif
}

static const proto::ProtoObject* py_getgid(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    return ctx->fromInteger(getgid());
#else
    return ctx->fromInteger(0);
#endif
}

static const proto::ProtoObject* py_getegid(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    return ctx->fromInteger(getegid());
#else
    return ctx->fromInteger(0);
#endif
}



static const proto::ProtoObject* py_exit(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    
    int status = 0;
    if (posArgs && posArgs->getSize(ctx) > 0) {
        const proto::ProtoObject* arg = posArgs->getAt(ctx, 0);
        if (arg && arg->isInteger(ctx)) {
            status = static_cast<int>(arg->asLong(ctx));
        }
    }
    exit(status);
    return PROTO_NONE;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx, PythonEnvironment* env) {
    if (!direntry_proto) {
        direntry_proto = ctx->newObject(false);
        if (env && env->getObjectPrototype()) {
            direntry_proto = direntry_proto->addParent(ctx, env->getObjectPrototype());
        }
        // Ensure direntry_proto is a fresh object and not polluting global Object prototype
        // In some protoCore versions, newObject(true) might return a shared object if not careful.
        // We set it explicitly to have no parent or a fresh one if possible.
        direntry_proto = direntry_proto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "is_dir"),
            ctx->fromMethod(const_cast<proto::ProtoObject*>(direntry_proto), py_direntry_is_dir));
        direntry_proto = direntry_proto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "is_file"),
            ctx->fromMethod(const_cast<proto::ProtoObject*>(direntry_proto), py_direntry_is_file));
        direntry_proto = direntry_proto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "is_symlink"),
            ctx->fromMethod(const_cast<proto::ProtoObject*>(direntry_proto), py_direntry_is_symlink));
        direntry_proto = direntry_proto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "stat"),
            ctx->fromMethod(const_cast<proto::ProtoObject*>(direntry_proto), py_direntry_stat));
        direntry_proto = direntry_proto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "inode"),
            ctx->fromMethod(const_cast<proto::ProtoObject*>(direntry_proto), py_direntry_inode));
        direntry_proto = direntry_proto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__fspath__"),
            ctx->fromMethod(const_cast<proto::ProtoObject*>(direntry_proto), py_direntry_fspath));
    }



    const proto::ProtoObject* mod = ctx->newObject(true);
    
    // Create Environ object
    const proto::ProtoObject* environProt = ctx->newObject(false);
    environProt = environProt->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__getitem__"), 
        ctx->fromMethod(const_cast<proto::ProtoObject*>(environProt), py_environ_getitem));
    environProt = environProt->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__setitem__"), 
        ctx->fromMethod(const_cast<proto::ProtoObject*>(environProt), py_environ_setitem));
    environProt = environProt->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__delitem__"), 
        ctx->fromMethod(const_cast<proto::ProtoObject*>(environProt), py_environ_delitem));
    environProt = environProt->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "keys"), 
        ctx->fromMethod(const_cast<proto::ProtoObject*>(environProt), py_environ_keys_method));

    const proto::ProtoObject* environObj = ctx->newObject(false);
    environObj = environObj->addParent(ctx, environProt);
    if (env && env->getObjectPrototype()) {
        mod = mod->addParent(ctx, env->getObjectPrototype());
        environProt = environProt->addParent(ctx, env->getObjectPrototype());
    }

    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "environ"), environObj);

    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "getenv"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_getenv));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "putenv"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_setenv));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "unsetenv"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_unsetenv));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "getcwd"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_getcwd));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "chdir"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_chdir));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "listdir"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_listdir));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "scandir"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_scandir));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "stat"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_stat));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "lstat"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_lstat));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "remove"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_remove));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "unlink"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_unlink));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "mkdir"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_mkdir));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "rename"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_rename));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "replace"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_replace));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "access"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_access));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "rmdir"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_rmdir));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "getuid"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_getuid));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "geteuid"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_geteuid));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "getgid"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_getgid));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "getegid"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_getegid));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "environ_keys"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_environ_keys));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "waitpid"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_waitpid));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "kill"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_kill));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "pipe"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_pipe));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "_exit"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_exit));

    // Common constants
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "F_OK"), ctx->fromInteger(0));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "R_OK"), ctx->fromInteger(4));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "W_OK"), ctx->fromInteger(2));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "X_OK"), ctx->fromInteger(1));

    // _have_functions (Minimal set for satisfying os.py)
    const proto::ProtoList* haveFuncs = ctx->newList();
    haveFuncs = haveFuncs->appendLast(ctx, ctx->fromUTF8String("HAVE_FACCESSAT"));
    haveFuncs = haveFuncs->appendLast(ctx, ctx->fromUTF8String("HAVE_FSTATAT"));
    haveFuncs = haveFuncs->appendLast(ctx, ctx->fromUTF8String("HAVE_OPENAT"));
    haveFuncs = haveFuncs->appendLast(ctx, ctx->fromUTF8String("HAVE_FDOPENDIR"));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "_have_functions"), haveFuncs->asObject(ctx));

    // Export keys and all for 'from posix import *'
    const proto::ProtoList* keys = ctx->newList();
    keys = keys->appendLast(ctx, ctx->fromUTF8String("environ"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("getenv"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("putenv"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("unsetenv"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("getcwd"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("chdir"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("listdir"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("remove"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("unlink"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("mkdir"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("rename"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("replace"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("access"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("rmdir"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("getuid"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("geteuid"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("getgid"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("getegid"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("environ_keys"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("waitpid"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("kill"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("pipe"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("_exit"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("scandir"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("stat"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("lstat"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("F_OK"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("R_OK"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("W_OK"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("X_OK"));
    keys = keys->appendLast(ctx, ctx->fromUTF8String("_have_functions"));
    
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__keys__"), keys->asObject(ctx));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__all__"), keys->asObject(ctx));

    return mod;
}

} // namespace os_module
} // namespace protoPython
