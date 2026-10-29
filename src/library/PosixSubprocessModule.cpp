#include <protoPython/PosixSubprocessModule.h>
#include <protoPython/PythonEnvironment.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <csignal>
#include <cstdio>
#include <cerrno>
#include <vector>
#include <string>
#include <string.h>

namespace protoPython {
namespace posixsubprocess_module {

// subprocess.py passes args/executable_list/env_list as `bytes`
// after fsencode, so the child has to accept both str and bytes
// payloads.  This mirrors OsModule's extractRawBytes helper.
static bool extractStringOrBytes(
    proto::ProtoContext* ctx, const proto::ProtoObject* obj, std::string& out) {
    if (!obj) return false;
    if (obj->isString(ctx)) {
        obj->asString(ctx)->toUTF8String(ctx, out);
        return true;
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoString* dataKey = env ? env->getDataString()
        : PythonEnvironment::getInternedString(ctx, "__data__");
    const proto::ProtoObject* d = obj->getAttribute(ctx, dataKey);
    if (!d) return false;
    if (d->isByteBuffer(ctx)) {
        const proto::ProtoByteBuffer* bb = d->asByteBuffer(ctx);
        if (bb) { out.assign(bb->getBuffer(ctx), bb->getSize(ctx)); return true; }
    } else if (d->isString(ctx)) {
        d->asString(ctx)->toUTF8String(ctx, out);
        return true;
    }
    return false;
}

static const proto::ProtoObject* py_fork_exec(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoArguments* posArgs,
    const proto::ProtoKeywordArguments* /*kwargs*/) {
    
    // STRUCT-288: CPython 3.14 subprocess.py passes 22 positional
    // arguments to _fork_exec (args, executable_list, close_fds,
    // fds_to_keep, cwd, env_list, p2cread, p2cwrite, c2pread,
    // c2pwrite, errread, errwrite, errpipe_read, errpipe_write,
    // restore_signals, start_new_session, process_group, gid, gids,
    // uid, umask, preexec_fn).  The previous "expected 26" check
    // rejected every legitimate call, breaking subprocess.Popen
    // entirely.
    if (!posArgs || posArgs->getSize(ctx) < 22) {
        PythonEnvironment::fromContext(ctx)->raiseRuntimeError(ctx, "_posixsubprocess.fork_exec expected at least 22 arguments");
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
    const proto::ProtoObject* restoreObj = posArgs->getAt(ctx, 14);
    const bool restoreSignals = restoreObj == PROTO_TRUE
        || (restoreObj && restoreObj->isInteger(ctx) && restoreObj->asLong(ctx) != 0);
    int errpipe_read = posArgs->getAt(ctx, 12) ? posArgs->getAt(ctx, 12)->asLong(ctx) : -1;
    int errpipe_write = posArgs->getAt(ctx, 13) ? posArgs->getAt(ctx, 13)->asLong(ctx) : -1;

    size_t argsCount = 0;
    if (argsObj && ::protoPython::PythonEnvironment::isTupleValue(ctx, argsObj)) argsCount = ::protoPython::PythonEnvironment::tupleView(ctx, argsObj)->getSize(ctx);
    else if (argsObj && ::protoPython::listStorage(ctx, argsObj)) argsCount = ::protoPython::listStorage(ctx, argsObj)->getSize(ctx);

    size_t execCount = 0;
    if (execListObj && ::protoPython::PythonEnvironment::isTupleValue(ctx, execListObj)) execCount = ::protoPython::PythonEnvironment::tupleView(ctx, execListObj)->getSize(ctx);

    size_t envCount = 0;
    bool hasEnv = false;
    if (envListObj && ::protoPython::listStorage(ctx, envListObj)) {
        envCount = ::protoPython::listStorage(ctx, envListObj)->getSize(ctx);
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

    // Everything the child needs is built here, before fork(): after fork()
    // only async-signal-safe calls are allowed, and the child must not
    // touch the runtime (its collector thread does not exist there).
    auto toStrings = [&](const proto::ProtoObject* seq, size_t n) {
        std::vector<std::string> out;
        out.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            const proto::ProtoObject* elem = ::protoPython::PythonEnvironment::isTupleValue(ctx, seq)
                ? ::protoPython::PythonEnvironment::tupleView(ctx, seq)->getAt(ctx, i)
                : ::protoPython::listStorage(ctx, seq)->getAt(ctx, i);
            std::string str;
            extractStringOrBytes(ctx, elem, str);
            out.push_back(std::move(str));
        }
        return out;
    };
    std::vector<std::string> argStrs = toStrings(argsObj, argsCount);
    std::vector<std::string> envStrs = hasEnv ? toStrings(envListObj, envCount) : std::vector<std::string>();
    std::vector<std::string> execPaths = toStrings(execListObj, execCount);
    std::vector<char*> c_args, c_env;
    for (auto& a : argStrs) c_args.push_back(a.data());
    c_args.push_back(nullptr);
    for (auto& e : envStrs) c_env.push_back(e.data());
    c_env.push_back(nullptr);

    pid_t pid = fork();
    if (pid == -1) {
        PythonEnvironment::fromContext(ctx)->raiseOSError(ctx, errno, strerror(errno), "");
        return nullptr;
    }

    if (pid == 0) {
        // Child process.  os.pipe() descriptors are non-inheritable: dup2
        // clears FD_CLOEXEC on the copy, and one already in place has it
        // cleared here, as _posixsubprocess does.
        auto toStd = [](int fd, int target) {
            if (fd == -1) return;
            if (fd != target) { dup2(fd, target); return; }
            const int fl = fcntl(fd, F_GETFD);
            if (fl != -1) fcntl(fd, F_SETFD, fl & ~FD_CLOEXEC);
        };
        toStd(p2cread, 0);
        toStd(c2pwrite, 1);
        toStd(errwrite, 2);
        // restore_signals: the interpreter ignores SIGPIPE (as CPython does);
        // the child gets the default action back.
        if (restoreSignals) signal(SIGPIPE, SIG_DFL);

        // A failure is reported on the error pipe in the form subprocess.py
        // parses ("OSError:<hex errno>:<message>"), so the parent raises the
        // right OSError (FileNotFoundError, PermissionError, ...).
        auto fail = [&](int err, const char* what) {
            if (errpipe_write != -1) {
                char buf[64];
                int n = snprintf(buf, sizeof(buf), "OSError:%x:%s", err, what);
                if (n > 0) { ssize_t w = write(errpipe_write, buf, static_cast<size_t>(n)); (void)w; }
            }
            _exit(255);
        };

        if (hasCwd && chdir(cwdStr.c_str()) != 0) fail(errno, "noexec:chdir");

        // Try each entry of executable_list (os.fsencode + os.path.join
        // results), keeping the most informative error, as CPython does.
        int saved = ENOENT;
        for (const auto& path : execPaths) {
            if (path.empty()) continue;
            if (hasEnv) execve(path.c_str(), c_args.data(), c_env.data());
            else execv(path.c_str(), c_args.data());
            if (errno != ENOENT && errno != ENOTDIR && saved == ENOENT) saved = errno;
        }
        if (execPaths.empty() && !argStrs.empty()) {
            execvp(c_args[0], c_args.data());
            saved = errno;
        }
        fail(saved, "");
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
