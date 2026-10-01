#include <protoPython/SelectModule.h>
#include <protoPython/PythonEnvironment.h>
#if defined(_WIN32)
// select() on Windows waits on sockets only, as CPython's select.select there.
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
// <algorithm> for the std::max(initializer_list) overload used below. GCC 13
// reaches it transitively through another header; GCC 14 does not, so a
// Fedora 41 build fails without this include.
#include <algorithm>
#include <vector>
#include <string>
#include <string.h>

namespace protoPython {
namespace select_module {

// Helper to fill a fd_set from a ProtoObject (List, Tuple, or Set).
// selectors.SelectSelector keeps the registered fds in a `set`, and
// CPython's select.select() accepts any iterable — we walk via the
// concrete collection API instead of building a Python-level
// iterator to avoid recursive interpreter dispatch on the hot path.
static int fillFdSet(proto::ProtoContext* ctx, const proto::ProtoObject* obj, fd_set* set) {
    int maxfd = -1;
    if (!obj) return maxfd;

    auto process = [&](const proto::ProtoObject* fd) {
        if (fd && fd->isInteger(ctx)) {
            int f = (int)fd->asLong(ctx);
#if defined(_WIN32)
            // A Windows fd_set is a list of up to FD_SETSIZE sockets, not a bitmap.
            if (f >= 0 && set->fd_count < FD_SETSIZE) {
                FD_SET(static_cast<SOCKET>(f), set);
                if (f > maxfd) maxfd = f;
            }
#else
            if (f >= 0 && f < FD_SETSIZE) {
                FD_SET(f, set);
                if (f > maxfd) maxfd = f;
            }
#endif
        }
    };

    if (const proto::ProtoList* list = obj->asList(ctx)) {
        for (size_t i = 0; i < list->getSize(ctx); ++i) process(list->getAt(ctx, i));
        return maxfd;
    }
    if (const proto::ProtoTuple* tup = obj->asTuple(ctx)) {
        for (size_t i = 0; i < tup->getSize(ctx); ++i) process(tup->getAt(ctx, i));
        return maxfd;
    }
    if (obj->isSet(ctx)) {
        const proto::ProtoSet* s = obj->asSet(ctx);
        if (s) {
            auto* it = const_cast<proto::ProtoSetIterator*>(s->getIterator(ctx));
            while (it && it->hasNext(ctx)) {
                process(it->next(ctx));
                it = const_cast<proto::ProtoSetIterator*>(it->advance(ctx));
            }
        }
    }
    return maxfd;
}

#if defined(_WIN32)
// Windows: Winsock's select() takes sockets only. A number that is a C runtime
// descriptor is a pipe (os.pipe(), the self-pipe of asyncio's event loop),
// which is polled with PeekNamedPipe -- readable when it holds data or its
// writer is gone -- or a file or console, always ready, as poll() reports
// them on POSIX. Other numbers are sockets, asked with a zero-timeout select().
// The sets are polled every millisecond until something is ready or the
// timeout (negative: none) expires. On return the sets hold the ready fds.
static int windowsSelect(proto::ProtoContext* ctx, fd_set& rfds, fd_set& wfds, fd_set& efds,
                         double timeoutSec, int& err) {
    static std::once_flag wsaOnce;
    std::call_once(wsaOnce, [] { WSADATA data; WSAStartup(MAKEWORD(2, 2), &data); });
    const fd_set inR = rfds, inW = wfds, inX = efds;
    const ULONGLONG start = GetTickCount64();
    proto::ProtoContext::UnmanagedScope u(ctx);
    for (;;) {
        FD_ZERO(&rfds); FD_ZERO(&wfds); FD_ZERO(&efds);
        fd_set sockR, sockW, sockX;
        FD_ZERO(&sockR); FD_ZERO(&sockW); FD_ZERO(&sockX);
        bool anySocket = false;
        int ready = 0;
        auto crtHandle = [](SOCKET s) {
            return reinterpret_cast<HANDLE>(_get_osfhandle(static_cast<int>(s)));
        };
        for (u_int k = 0; k < inR.fd_count; ++k) {
            const SOCKET s = inR.fd_array[k];
            const HANDLE h = crtHandle(s);
            if (h == INVALID_HANDLE_VALUE) { FD_SET(s, &sockR); anySocket = true; continue; }
            bool readable = true;
            if (GetFileType(h) == FILE_TYPE_PIPE) {
                DWORD avail = 0;
                readable = !PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr) || avail > 0;
            }
            if (readable) { FD_SET(s, &rfds); ++ready; }
        }
        for (u_int k = 0; k < inW.fd_count; ++k) {
            const SOCKET s = inW.fd_array[k];
            if (crtHandle(s) == INVALID_HANDLE_VALUE) { FD_SET(s, &sockW); anySocket = true; continue; }
            FD_SET(s, &wfds);
            ++ready;
        }
        for (u_int k = 0; k < inX.fd_count; ++k) {
            const SOCKET s = inX.fd_array[k];
            if (crtHandle(s) == INVALID_HANDLE_VALUE) { FD_SET(s, &sockX); anySocket = true; }
        }
        if (anySocket) {
            timeval zero{0, 0};
            const int n = ::select(0, &sockR, &sockW, &sockX, &zero);
            if (n == SOCKET_ERROR) {
                const int wsa = WSAGetLastError();
                err = (wsa == WSAENOTSOCK) ? EBADF : (wsa == WSAEINTR) ? EINTR : EINVAL;
                return -1;
            }
            for (u_int k = 0; k < sockR.fd_count; ++k) { FD_SET(sockR.fd_array[k], &rfds); ++ready; }
            for (u_int k = 0; k < sockW.fd_count; ++k) { FD_SET(sockW.fd_array[k], &wfds); ++ready; }
            for (u_int k = 0; k < sockX.fd_count; ++k) { FD_SET(sockX.fd_array[k], &efds); ++ready; }
        }
        if (ready > 0) return ready;
        if (timeoutSec >= 0.0
            && static_cast<double>(GetTickCount64() - start) >= timeoutSec * 1000.0) {
            return 0;
        }
        Sleep(1);
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

    if (!posArgs || posArgs->getSize(ctx) < 3) {
        PythonEnvironment::fromContext(ctx)->raiseRuntimeError(ctx, "select() requires at least 3 arguments");
        return nullptr;
    }

    const proto::ProtoObject* rObj = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* wObj = posArgs->getAt(ctx, 1);
    const proto::ProtoObject* xObj = posArgs->getAt(ctx, 2);

    double timeout_sec = -1.0;
    if (posArgs->getSize(ctx) >= 4) {
        const proto::ProtoObject* to = posArgs->getAt(ctx, 3);
        if (to && !to->isNone(ctx)) {
            if (to->isFloat(ctx)) timeout_sec = to->asDouble(ctx);
            else if (to->isInteger(ctx)) timeout_sec = (double)to->asLong(ctx);
        }
    }

    fd_set rfds, wfds, efds;
    FD_ZERO(&rfds); FD_ZERO(&wfds); FD_ZERO(&efds);
    int maxfd = -1;

    int m1 = fillFdSet(ctx, rObj, &rfds);
    int m2 = fillFdSet(ctx, wObj, &wfds);
    int m3 = fillFdSet(ctx, xObj, &efds);
    maxfd = std::max({m1, m2, m3});

    struct timeval tv;
    struct timeval* tvp = nullptr;
    if (timeout_sec >= 0.0) {
        tv.tv_sec = (proto::proto_long)timeout_sec;
        tv.tv_usec = (proto::proto_long)((timeout_sec - tv.tv_sec) * 1e6);
        tvp = &tv;
    }

    // 2026-05-25: `select` is THE prototypical blocking syscall — its
    // whole purpose is to suspend until a file descriptor is ready or
    // the timeout fires. Bracket it in a protoCore unmanaged region
    // so a concurrent GC cycle is not pinned for the wait duration.
    // Save errno before returnFromUnmanaged because the protoCore
    // path may run atomic ops between the syscall return and the
    // moment we examine errno.
    int ret;
    int err = 0;
#if defined(_WIN32)
    (void)tvp;
    ret = windowsSelect(ctx, rfds, wfds, efds, timeout_sec, err);
#else
    {
        proto::ProtoContext::UnmanagedScope u(ctx);
        ret = ::select(maxfd + 1, &rfds, &wfds, &efds, tvp);
        err = (ret < 0) ? errno : 0;
    }
#endif
    if (ret < 0) {
        PythonEnvironment::fromContext(ctx)->raiseOSError(ctx, err, strerror(err), "");
        return nullptr;
    }

    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    auto makeResultList = [&](const proto::ProtoObject* original, fd_set& set) {
        const proto::ProtoList* result = ctx->newList();
        if (!original) return PythonEnvironment::wrapList(ctx, result);
        auto check = [&](const proto::ProtoObject* fd) {
            if (fd && fd->isInteger(ctx) && FD_ISSET((int)fd->asLong(ctx), &set)) {
                result = result->appendLast(ctx, fd);
            }
        };
        if (const proto::ProtoList* list = original->asList(ctx)) {
            for (size_t i = 0; i < list->getSize(ctx); ++i) check(list->getAt(ctx, i));
        } else if (const proto::ProtoTuple* tup = original->asTuple(ctx)) {
            for (size_t i = 0; i < tup->getSize(ctx); ++i) check(tup->getAt(ctx, i));
        } else if (original->isSet(ctx)) {
            const proto::ProtoSet* s = original->asSet(ctx);
            if (s) {
                auto* it = const_cast<proto::ProtoSetIterator*>(s->getIterator(ctx));
                while (it && it->hasNext(ctx)) {
                    check(it->next(ctx));
                    it = const_cast<proto::ProtoSetIterator*>(it->advance(ctx));
                }
            }
        }
        return PythonEnvironment::wrapList(ctx, result);
    };

    const proto::ProtoList* tuple = ctx->newList();
    tuple = tuple->appendLast(ctx, makeResultList(rObj, rfds));
    tuple = tuple->appendLast(ctx, makeResultList(wObj, wfds));
    tuple = tuple->appendLast(ctx, makeResultList(xObj, efds));
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
