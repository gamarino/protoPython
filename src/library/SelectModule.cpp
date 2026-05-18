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
            if (f >= 0 && f < FD_SETSIZE) {
                FD_SET(f, set);
                if (f > maxfd) maxfd = f;
            }
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
        tv.tv_sec = (long)timeout_sec;
        tv.tv_usec = (long)((timeout_sec - tv.tv_sec) * 1e6);
        tvp = &tv;
    }

    int ret = ::select(maxfd + 1, &rfds, &wfds, &efds, tvp);
    if (ret < 0) {
        PythonEnvironment::fromContext(ctx)->raiseOSError(ctx, errno, strerror(errno), "");
        return nullptr;
    }

    auto makeResultList = [&](const proto::ProtoObject* original, fd_set& set) {
        const proto::ProtoList* result = ctx->newList();
        if (!original) return result->asObject(ctx);
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
        return result->asObject(ctx);
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
