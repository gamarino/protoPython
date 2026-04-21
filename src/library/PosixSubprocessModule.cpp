#include <protoPython/PosixSubprocessModule.h>
#include <protoPython/PythonEnvironment.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <vector>
#include <string>
#include <string.h>

namespace protoPython {
namespace posixsubprocess_module {

static const proto::ProtoObject* py_fork_exec(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    
    // We expect at least 26 arguments based on CPython 3.14 signature
    if (!posArgs || posArgs->getSize(ctx) < 26) {
        PythonEnvironment::fromContext(ctx)->raiseRuntimeError(ctx, "_posixsubprocess.fork_exec expected at least 26 arguments");
        return nullptr;
    }

    const proto::ProtoObject* argsObj = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* execListObj = posArgs->getAt(ctx, 1);
    // ... we can skip some unused features for our simplified version
    const proto::ProtoObject* cwdObj = posArgs->getAt(ctx, 4);
    const proto::ProtoObject* envListObj = posArgs->getAt(ctx, 5);

    int p2cread = posArgs->getAt(ctx, 6) ? posArgs->getAt(ctx, 6)->asLong(ctx) : -1;
    int p2cwrite = posArgs->getAt(ctx, 7) ? posArgs->getAt(ctx, 7)->asLong(ctx) : -1;
    int c2pread = posArgs->getAt(ctx, 8) ? posArgs->getAt(ctx, 8)->asLong(ctx) : -1;
    int c2pwrite = posArgs->getAt(ctx, 9) ? posArgs->getAt(ctx, 9)->asLong(ctx) : -1;
    int errread = posArgs->getAt(ctx, 10) ? posArgs->getAt(ctx, 10)->asLong(ctx) : -1;
    int errwrite = posArgs->getAt(ctx, 11) ? posArgs->getAt(ctx, 11)->asLong(ctx) : -1;
    int errpipe_read = posArgs->getAt(ctx, 12) ? posArgs->getAt(ctx, 12)->asLong(ctx) : -1;
    int errpipe_write = posArgs->getAt(ctx, 13) ? posArgs->getAt(ctx, 13)->asLong(ctx) : -1;

    std::vector<std::string> argsStr;
    if (argsObj && argsObj->isTuple(ctx)) {
        const proto::ProtoTuple* t = argsObj->asTuple(ctx);
        for (size_t i = 0; i < t->getSize(ctx); ++i) {
            const proto::ProtoObject* elem = t->getAt(ctx, i);
            if (elem) {
                std::string s;
                if (elem->isString(ctx)) elem->asString(ctx)->toUTF8String(ctx, s);
                argsStr.push_back(s);
            }
        }
    } else if (argsObj && argsObj->asList(ctx)) {
        const proto::ProtoList* t = argsObj->asList(ctx);
        for (size_t i = 0; i < t->getSize(ctx); ++i) {
            const proto::ProtoObject* elem = t->getAt(ctx, i);
            if (elem) {
                std::string s;
                if (elem->isString(ctx)) elem->asString(ctx)->toUTF8String(ctx, s);
                argsStr.push_back(s);
            }
        }
    }

    std::vector<std::string> execListStr;
    if (execListObj && execListObj->isTuple(ctx)) {
        const proto::ProtoTuple* t = execListObj->asTuple(ctx);
        for (size_t i = 0; i < t->getSize(ctx); ++i) {
            const proto::ProtoObject* elem = t->getAt(ctx, i);
            if (elem) {
                std::string s;
                if (elem->isString(ctx)) elem->asString(ctx)->toUTF8String(ctx, s);
                execListStr.push_back(s);
            }
        }
    }

    std::vector<std::string> envListStr;
    bool hasEnv = false;
    if (envListObj && envListObj->asList(ctx)) {
        hasEnv = true;
        const proto::ProtoList* t = envListObj->asList(ctx);
        for (size_t i = 0; i < t->getSize(ctx); ++i) {
            const proto::ProtoObject* elem = t->getAt(ctx, i);
            if (elem) {
                std::string s;
                if (elem->isString(ctx)) elem->asString(ctx)->toUTF8String(ctx, s);
                envListStr.push_back(s);
            }
        }
    }

    std::string cwdStr;
    bool hasCwd = false;
    if (cwdObj && !cwdObj->isNone(ctx)) {
        if (cwdObj->isString(ctx)) {
            cwdObj->asString(ctx)->toUTF8String(ctx, cwdStr);
            hasCwd = true;
        }
    }

    pid_t pid = fork();
    if (pid == -1) {
        PythonEnvironment::fromContext(ctx)->raiseOSError(ctx, errno, strerror(errno), "");
        return nullptr;
    }

    if (pid == 0) {
        // Child process
        if (p2cread != -1 && p2cread != 0) { dup2(p2cread, 0); }
        if (c2pwrite != -1 && c2pwrite != 1) { dup2(c2pwrite, 1); }
        if (errwrite != -1 && errwrite != 2) { dup2(errwrite, 2); }

        if (hasCwd) {
            if (chdir(cwdStr.c_str()) != 0) {
                // Ignore chdir failure for now, or report through errpipe
                _exit(1);
            }
        }

        std::vector<char*> c_args;
        for (auto& s : argsStr) {
            c_args.push_back(const_cast<char*>(s.c_str()));
        }
        c_args.push_back(nullptr);

        std::vector<char*> c_env;
        if (hasEnv) {
            for (auto& s : envListStr) {
                c_env.push_back(const_cast<char*>(s.c_str()));
            }
            c_env.push_back(nullptr);
        }

        for (auto& exec_path : execListStr) {
            if (hasEnv) {
                execve(exec_path.c_str(), c_args.data(), c_env.data());
            } else {
                execv(exec_path.c_str(), c_args.data());
            }
        }
        
        // If we reach here, exec failed
        _exit(255);
    }

    // Parent process
    return ctx->fromInteger(pid);
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx, PythonEnvironment* env) {
    const proto::ProtoObject* mod = env && env->getObjectPrototype() ? env->getObjectPrototype()->newChild(ctx, true) : ctx->newObject(false);
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "fork_exec"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_fork_exec));
    return mod;
}

} // namespace posixsubprocess_module
} // namespace protoPython
