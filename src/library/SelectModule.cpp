#include <protoPython/SelectModule.h>
#include <protoPython/PythonEnvironment.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <string.h>

namespace protoPython {
namespace select_module {

// Helper to read a list/tuple of FDs from a ProtoObject
static std::vector<int> extractFdSet(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    std::vector<int> fds;
    if (!obj) return fds;
    const proto::ProtoList* list = obj->asList(ctx);
    if (!list) {
        const proto::ProtoTuple* tup = obj->asTuple(ctx);
        if (tup) {
            for (size_t i = 0; i < tup->getSize(ctx); ++i) {
                const proto::ProtoObject* fd = tup->getAt(ctx, i);
                if (fd && fd->isInteger(ctx)) fds.push_back((int)fd->asLong(ctx));
            }
        }
    } else {
        for (size_t i = 0; i < list->getSize(ctx); ++i) {
            const proto::ProtoObject* fd = list->getAt(ctx, i);
            if (fd && fd->isInteger(ctx)) fds.push_back((int)fd->asLong(ctx));
        }
    }
    return fds;
}

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

    auto rList = extractFdSet(ctx, posArgs->getAt(ctx, 0));
    auto wList = extractFdSet(ctx, posArgs->getAt(ctx, 1));
    auto xList = extractFdSet(ctx, posArgs->getAt(ctx, 2));

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
    int maxfd = 0;

    for (int fd : rList) { FD_SET(fd, &rfds); if (fd > maxfd) maxfd = fd; }
    for (int fd : wList) { FD_SET(fd, &wfds); if (fd > maxfd) maxfd = fd; }
    for (int fd : xList) { FD_SET(fd, &efds); if (fd > maxfd) maxfd = fd; }

    struct timeval tv;
    struct timeval* tvp = nullptr;
    if (timeout_sec >= 0.0) {
        tv.tv_sec = (long)timeout_sec;
        tv.tv_usec = (long)((timeout_sec - tv.tv_sec) * 1e6);
        tvp = &tv;
    }

    int ret = ::select(maxfd + 1, &rfds, &wfds, &efds, tvp);
    if (ret < 0) {
        PythonEnvironment::fromContext(ctx)->raiseOSError(ctx, errno, strerror(errno), "");
        return nullptr;
    }

    auto makeFdList = [&](const std::vector<int>& fds, fd_set& set) {
        const proto::ProtoList* result = ctx->newList();
        for (int fd : fds) {
            if (FD_ISSET(fd, &set)) {
                result = result->appendLast(ctx, ctx->fromInteger(fd));
            }
        }
        return result->asObject(ctx);
    };

    const proto::ProtoList* tuple = ctx->newList();
    tuple = tuple->appendLast(ctx, makeFdList(rList, rfds));
    tuple = tuple->appendLast(ctx, makeFdList(wList, wfds));
    tuple = tuple->appendLast(ctx, makeFdList(xList, efds));
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
