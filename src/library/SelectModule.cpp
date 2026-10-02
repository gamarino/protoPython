// select.select over sockets, pipes and files.
//
// Windows: FD_SETSIZE is raised to 512 before any Winsock header, as CPython
// does, and a set larger than that raises ValueError. Must come first.
#if defined(_WIN32)
#ifndef FD_SETSIZE
#define FD_SETSIZE 512
#endif
#endif
#include <protoPython/SelectModule.h>
#include <protoPython/PythonEnvironment.h>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#pragma comment(lib, "ws2_32")
#include <cerrno>
#include <mutex>
#else
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <cmath>
#include <vector>
#include <string>
#include <string.h>
#if defined(_WIN32)
#include "PosixCompat.h"
#endif

namespace protoPython {
namespace select_module {

// The descriptors of one of select()'s three arguments (a list, tuple or set
// of ints or of objects with fileno()), in order.
static bool collectFds(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                       std::vector<const proto::ProtoObject*>& items, std::vector<long long>& fds) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    auto process = [&](const proto::ProtoObject* item) -> bool {
        const proto::ProtoObject* fdObj = item;
        if (!item || !item->isInteger(ctx)) {
            const proto::ProtoObject* r = env ? env->callMethod(item, "fileno", {}) : nullptr;
            if (!r || !r->isInteger(ctx)) {
                if (env && !env->hasPendingException()) {
                    env->raiseTypeError(ctx, "argument must be an int, or have a fileno() method.");
                }
                return false;
            }
            fdObj = r;
        }
        const long long fd = fdObj->asLong(ctx);
        if (fd < 0) {
            if (env) env->raiseValueError(ctx, PythonEnvironment::newStr(ctx,
                "file descriptor cannot be a negative integer (" + std::to_string(fd) + ")"));
            return false;
        }
        items.push_back(item);
        fds.push_back(fd);
        return true;
    };
    if (!obj || obj == PROTO_NONE) return true;
    const proto::ProtoObject* data = env ? obj->getAttribute(ctx, env->getDataString()) : nullptr;
    const proto::ProtoList* list = obj->asList(ctx);
    if (!list && data) list = data->asList(ctx);
    const proto::ProtoTuple* tup = list ? nullptr : obj->asTuple(ctx);
    if (!list && !tup && data) tup = data->asTuple(ctx);
    if (list) {
        for (size_t i = 0; i < list->getSize(ctx); ++i) if (!process(list->getAt(ctx, static_cast<int>(i)))) return false;
        return true;
    }
    if (tup) {
        for (size_t i = 0; i < tup->getSize(ctx); ++i) if (!process(tup->getAt(ctx, static_cast<int>(i)))) return false;
        return true;
    }
    if (obj->isSet(ctx)) {
        const proto::ProtoSet* s = obj->asSet(ctx);
        if (s) {
            auto* it = const_cast<proto::ProtoSetIterator*>(s->getIterator(ctx));
            while (it && it->hasNext(ctx)) {
                if (!process(it->next(ctx))) return false;
                it = const_cast<proto::ProtoSetIterator*>(it->advance(ctx));
            }
        }
        return true;
    }
    if (env) env->raiseTypeError(ctx, "arguments 1-3 must be sequences");
    return false;
}

#if defined(_WIN32)
// Windows. Winsock's select() takes sockets only; protoPython's descriptors
// are C runtime descriptors (os.pipe(), files, the console). Each number is
// classified once:
//   - an open C runtime descriptor: its OS handle decides -- a socket handle
//     (a descriptor made from a socket) is a socket, a pipe is a pipe, a file
//     or console is always ready (as poll() reports them on POSIX);
//   - otherwise a socket, if Winsock knows it (getsockopt SO_TYPE);
//   - otherwise EBADF.
// So a socket number is never taken for a descriptor unless a C runtime
// descriptor with that number is open, which is then the one meant.
//
// Waiting: sockets alone wait in Winsock's select() with the timeout. An
// anonymous pipe cannot be waited on for data, so with pipes in the sets the
// pipes are polled with PeekNamedPipe while any sockets wait in select() for
// a slice that grows from 1 to 20 ms, until something is ready or the timeout
// expires.
enum class Kind { Socket, Pipe, File };

struct Entry {
    Kind kind;
    SOCKET socket;
    HANDLE handle;
};

static bool isSocket(SOCKET s) {
    int type = 0;
    int len = sizeof(type);
    return getsockopt(s, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&type), &len) == 0;
}

static bool classify(long long fd, Entry& e) {
    intptr_t osf = -1;
    {
        ProtopyInvalidParameterScope reportInvalidParameters;
        if (fd <= 0x7fffffff) osf = _get_osfhandle(static_cast<int>(fd));
    }
    if (osf != -1 && osf != -2) {
        e.handle = reinterpret_cast<HANDLE>(osf);
        e.socket = static_cast<SOCKET>(osf);
        if (isSocket(e.socket)) { e.kind = Kind::Socket; return true; }
        e.kind = GetFileType(e.handle) == FILE_TYPE_PIPE ? Kind::Pipe : Kind::File;
        return true;
    }
    e.socket = static_cast<SOCKET>(fd);
    e.handle = INVALID_HANDLE_VALUE;
    if (isSocket(e.socket)) { e.kind = Kind::Socket; return true; }
    return false;
}

// Readable: data waiting, or the writer gone (end of file).
static bool pipeReadable(HANDLE h) {
    DWORD avail = 0;
    return !PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr) || avail > 0;
}

static int windowsSelect(proto::ProtoContext* ctx, const std::vector<long long> in[3],
                         std::vector<bool> ready[3], double timeoutSec, int& err) {
    static std::once_flag wsaOnce;
    std::call_once(wsaOnce, [] { WSADATA data; WSAStartup(MAKEWORD(2, 2), &data); });
    std::vector<Entry> entries[3];
    bool anyPipe = false;
    for (int s = 0; s < 3; ++s) {
        ready[s].assign(in[s].size(), false);
        for (long long fd : in[s]) {
            Entry e;
            if (!classify(fd, e)) { err = EBADF; return -1; }
            if (e.kind == Kind::Pipe && s == 0) anyPipe = true;
            entries[s].push_back(e);
        }
    }
    proto::ProtoContext::UnmanagedScope u(ctx);
    const ULONGLONG start = GetTickCount64();
    DWORD slice = 1;
    for (;;) {
        int count = 0;
        fd_set sets[3];
        bool anySocket = false;
        for (int s = 0; s < 3; ++s) {
            FD_ZERO(&sets[s]);
            for (size_t k = 0; k < entries[s].size(); ++k) {
                const Entry& e = entries[s][k];
                if (e.kind == Kind::Socket) {
                    FD_SET(e.socket, &sets[s]);
                    anySocket = true;
                } else if (s == 0) {
                    // Readability of a pipe; a file is always readable.
                    if (e.kind == Kind::File || pipeReadable(e.handle)) { ready[s][k] = true; ++count; }
                } else if (s == 1) {
                    // Pipes and files are always writable (a full pipe blocks
                    // the write, as on POSIX once select() said writable).
                    ready[s][k] = true;
                    ++count;
                }
            }
        }
        // Wait in select() when only sockets can become ready; otherwise poll.
        long waitMs;
        if (count > 0) {
            waitMs = 0;
        } else if (!anyPipe) {
            waitMs = timeoutSec < 0 ? -1 : static_cast<long>(std::ceil(timeoutSec * 1000.0));
        } else {
            waitMs = static_cast<long>(slice);
            if (timeoutSec >= 0) {
                const double left = timeoutSec * 1000.0 - static_cast<double>(GetTickCount64() - start);
                waitMs = std::max(0L, std::min(waitMs, static_cast<long>(std::ceil(left))));
            }
        }
        if (anySocket) {
            timeval tv{waitMs / 1000, (waitMs % 1000) * 1000};
            const int n = ::select(0, &sets[0], &sets[1], &sets[2], waitMs < 0 ? nullptr : &tv);
            if (n == SOCKET_ERROR) {
                const int wsa = WSAGetLastError();
                err = (wsa == WSAENOTSOCK) ? EBADF : (wsa == WSAEINTR) ? EINTR : EINVAL;
                return -1;
            }
            for (int s = 0; s < 3; ++s) {
                for (size_t k = 0; k < entries[s].size(); ++k) {
                    if (entries[s][k].kind == Kind::Socket && FD_ISSET(entries[s][k].socket, &sets[s])) {
                        ready[s][k] = true;
                        ++count;
                    }
                }
            }
        } else if (waitMs != 0) {
            // Nothing a select() can wait on: sleep the slice (with no
            // timeout and nothing that can become ready, that is forever).
            Sleep(waitMs < 0 ? 20 : static_cast<DWORD>(waitMs));
        }
        if (count > 0) return count;
        if (timeoutSec >= 0 && static_cast<double>(GetTickCount64() - start) >= timeoutSec * 1000.0) return 0;
        if (!anyPipe && anySocket && waitMs >= 0 && timeoutSec >= 0) return 0;
        slice = std::min<DWORD>(slice * 2, 20);
    }
}
#endif

// select(rlist, wlist, xlist[, timeout]) -> (rlist, wlist, xlist)
static const proto::ProtoObject* py_select(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!posArgs || posArgs->getSize(ctx) < 3) {
        if (env) env->raiseTypeError(ctx, "select expected at least 3 arguments");
        return nullptr;
    }

    double timeout_sec = -1.0;
    if (posArgs->getSize(ctx) >= 4) {
        const proto::ProtoObject* to = posArgs->getAt(ctx, 3);
        if (to && !to->isNone(ctx)) {
            if (to->isFloat(ctx)) timeout_sec = to->asDouble(ctx);
            else if (to->isInteger(ctx)) timeout_sec = (double)to->asLong(ctx);
            if (timeout_sec < 0) {
                if (env) env->raiseValueError(ctx, PythonEnvironment::newStr(ctx, "timeout must be non-negative"));
                return nullptr;
            }
        }
    }

    std::vector<const proto::ProtoObject*> items[3];
    std::vector<long long> fds[3];
    for (int s = 0; s < 3; ++s) {
        if (!collectFds(ctx, posArgs->getAt(ctx, s), items[s], fds[s])) return nullptr;
#if defined(_WIN32)
        // A Windows fd_set holds at most FD_SETSIZE sockets.
        if (fds[s].size() > FD_SETSIZE) {
            if (env) env->raiseValueError(ctx, PythonEnvironment::newStr(ctx, "too many file descriptors in select()"));
            return nullptr;
        }
#else
        // A POSIX fd_set is a bitmap of FD_SETSIZE descriptors.
        for (long long fd : fds[s]) {
            if (fd >= FD_SETSIZE) {
                if (env) env->raiseValueError(ctx, PythonEnvironment::newStr(ctx, "filedescriptor out of range in select()"));
                return nullptr;
            }
        }
#endif
    }

    // 2026-05-25: `select` is THE prototypical blocking syscall — its
    // whole purpose is to suspend until a file descriptor is ready or
    // the timeout fires. It runs in a protoCore unmanaged region so a
    // concurrent GC cycle is not pinned for the wait duration; errno is
    // saved before leaving it.
    std::vector<bool> ready[3];
    int ret;
    int err = 0;
#if defined(_WIN32)
    ret = windowsSelect(ctx, fds, ready, timeout_sec, err);
#else
    fd_set sets[3];
    int maxfd = -1;
    for (int s = 0; s < 3; ++s) {
        FD_ZERO(&sets[s]);
        for (long long fd : fds[s]) {
            FD_SET(static_cast<int>(fd), &sets[s]);
            maxfd = std::max(maxfd, static_cast<int>(fd));
        }
    }
    struct timeval tv;
    struct timeval* tvp = nullptr;
    if (timeout_sec >= 0.0) {
        tv.tv_sec = static_cast<time_t>(timeout_sec);
        tv.tv_usec = static_cast<suseconds_t>((timeout_sec - static_cast<double>(tv.tv_sec)) * 1e6);
        tvp = &tv;
    }
    {
        proto::ProtoContext::UnmanagedScope u(ctx);
        ret = ::select(maxfd + 1, &sets[0], &sets[1], &sets[2], tvp);
        err = (ret < 0) ? errno : 0;
    }
    if (ret >= 0) {
        for (int s = 0; s < 3; ++s) {
            ready[s].assign(fds[s].size(), false);
            for (size_t k = 0; k < fds[s].size(); ++k) ready[s][k] = FD_ISSET(static_cast<int>(fds[s][k]), &sets[s]) != 0;
        }
    }
#endif
    if (ret < 0) {
        if (env) env->raiseOSError(ctx, err, strerror(err), "");
        return nullptr;
    }

    const proto::ProtoList* tuple = ctx->newList();
    for (int s = 0; s < 3; ++s) {
        const proto::ProtoList* result = ctx->newList();
        for (size_t k = 0; k < items[s].size(); ++k) {
            if (ready[s][k]) result = result->appendLast(ctx, items[s][k]);
        }
        tuple = tuple->appendLast(ctx, PythonEnvironment::wrapList(ctx, result));
    }
    return ctx->newTupleFromList(tuple)->asObject(ctx);
}

// error class (subclass of OSError)
static const proto::ProtoObject* py_select_error_class = nullptr;

const proto::ProtoObject* initialize(proto::ProtoContext* ctx, PythonEnvironment* env) {
    auto sym = [&](const char* s) { return proto::ProtoString::createSymbol(ctx, s); };

    const proto::ProtoObject* mod = ctx->newObject(false);

    mod = mod->setAttribute(ctx, sym("select"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_select));

    // Constants
    mod = mod->setAttribute(ctx, sym("POLLIN"),   ctx->fromInteger(0x001));
    mod = mod->setAttribute(ctx, sym("POLLPRI"),  ctx->fromInteger(0x002));
    mod = mod->setAttribute(ctx, sym("POLLOUT"),  ctx->fromInteger(0x004));
    mod = mod->setAttribute(ctx, sym("POLLERR"),  ctx->fromInteger(0x008));
    mod = mod->setAttribute(ctx, sym("POLLHUP"),  ctx->fromInteger(0x010));
    mod = mod->setAttribute(ctx, sym("POLLNVAL"), ctx->fromInteger(0x020));

    // Expose EPOLLIN/EPOLLOUT for selectors.py feature probing;
    // they will only be used if hasattr(select,'epoll') is True,
    // which it won't be since we don't expose select.epoll.
    // We don't need to add epoll/poll objects since selectors.py
    // will fall through to SelectSelector.

    // error = OSError alias (for compatibility)
    // We don't expose select.epoll/poll so selectors.py falls through to SelectSelector.

    return mod;
}

} // namespace select_module
} // namespace protoPython
