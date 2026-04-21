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

    size_t argsCount = 0;
    if (argsObj && argsObj->isTuple(ctx)) argsCount = argsObj->asTuple(ctx)->getSize(ctx);
    else if (argsObj && argsObj->asList(ctx)) argsCount = argsObj->asList(ctx)->getSize(ctx);

    size_t execCount = 0;
    if (execListObj && execListObj->isTuple(ctx)) execCount = execListObj->asTuple(ctx)->getSize(ctx);

    size_t envCount = 0;
    bool hasEnv = false;
    if (envListObj && envListObj->asList(ctx)) {
        envCount = envListObj->asList(ctx)->getSize(ctx);
        hasEnv = true;
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
                _exit(1);
            }
        }

        // Build args array
        char** c_args = new char*[argsCount + 1];
        for (size_t i = 0; i < argsCount; ++i) {
            const proto::ProtoObject* elem = argsObj->isTuple(ctx) ? argsObj->asTuple(ctx)->getAt(ctx, i) : argsObj->asList(ctx)->getAt(ctx, i);
            std::string s;
            if (elem && elem->isString(ctx)) elem->asString(ctx)->toUTF8String(ctx, s);
            // We need to keep the string alive. 
            // In a child process after fork, we don't care much about memory leaks before exec.
            c_args[i] = strdup(s.c_str());
        }
        c_args[argsCount] = nullptr;

        // Build env array
        char** c_env = nullptr;
        if (hasEnv) {
            c_env = new char*[envCount + 1];
            for (size_t i = 0; i < envCount; ++i) {
                const proto::ProtoObject* elem = envListObj->asList(ctx)->getAt(ctx, i);
                std::string s;
                if (elem && elem->isString(ctx)) elem->asString(ctx)->toUTF8String(ctx, s);
                c_env[i] = strdup(s.c_str());
            }
            c_env[envCount] = nullptr;
        }

        for (size_t i = 0; i < execCount; ++i) {
            const proto::ProtoObject* execPathObj = execListObj->asTuple(ctx)->getAt(ctx, i);
            std::string exec_path;
            if (execPathObj && execPathObj->isString(ctx)) execPathObj->asString(ctx)->toUTF8String(ctx, exec_path);
            
            if (hasEnv) {
                execve(exec_path.c_str(), c_args, c_env);
            } else {
                execv(exec_path.c_str(), c_args);
            }
        }
        
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
