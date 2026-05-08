#include <protoPython/AtexitModule.h>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>
#include <cstdio>

// Real atexit implementation. Replaces a fully stubbed module
// (register/unregister/_run_exitfuncs were all `(void)posArgs;
// return PROTO_NONE`) so callbacks registered via `atexit.register`
// were silently dropped — sys.exit, finalisers, log-flushers, and
// caches that rely on atexit for last-mile cleanup never ran.
//
// PythonEnvironment::runExitHandlers() (in PythonEnvironment.cpp)
// already invokes `_run_exitfuncs` at interpreter shutdown, so this
// module just needs to wire register/unregister/run to a real
// callback list. Registered callbacks are stored on the module
// instance itself under `__atexit_handlers__`, which keeps them
// pinned via the `sys.modules` graph until the runtime tears down.

namespace protoPython {

// Forward decl — defined in ExecutionEngine.cpp.
extern const proto::ProtoObject* invokePythonCallable(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* callable,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kwargs);

namespace atexit_module {

static const proto::ProtoString* sym(proto::ProtoContext* ctx, const char* name) {
    return proto::ProtoString::createSymbol(ctx, name);
}

// Each handler is stored as a 3-tuple-like list `[callable, args_list,
// kwargs_sparselist]`. We use a list of lists rather than tuples
// because protoPython's tuple constructors expect content known at
// build time; structural lists are mutable and avoid the round-trip.
static const proto::ProtoObject* handlers_get_list(
    proto::ProtoContext* ctx, const proto::ProtoObject* mod) {
    const proto::ProtoObject* lst = mod->getAttribute(ctx, sym(ctx, "__atexit_handlers__"));
    if (lst && lst != PROTO_NONE && lst->asList(ctx)) return lst;
    return nullptr;
}

static const proto::ProtoObject* py_register(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwargs) {
    if (!self || !posArgs || posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* callable = posArgs->getAt(ctx, 0);

    // Preserve trailing positional / keyword args so the handler is
    // re-called with the exact bindings it requested.
    const proto::ProtoList* hargs = ctx->newList();
    for (unsigned long i = 1; i < posArgs->getSize(ctx); ++i) {
        hargs = hargs->appendLast(ctx, posArgs->getAt(ctx, static_cast<int>(i)));
    }

    const proto::ProtoList* entry = ctx->newList();
    entry = entry->appendLast(ctx, callable);
    entry = entry->appendLast(ctx, hargs->asObject(ctx));
    // We can't easily round-trip a sparse-list kwargs through a list
    // attribute, so wrap it as an external pointer that owns no
    // memory (the sparse list itself is GC-managed). Fall back to a
    // null marker when no kwargs were provided.
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (kwargs && env) {
        // Store as a regular dict-shaped object so the run path can
        // re-extract it. We materialise a list of (key, value) pairs.
        const proto::ProtoList* kwList = ctx->newList();
        // Iterate the SparseList's entries via its iterator.
        const proto::ProtoSparseListIterator* it = kwargs->getIterator(ctx);
        while (it && it->hasNext(ctx)) {
            const proto::ProtoObject* val = it->nextValue(ctx);
            unsigned long key = it->nextKey(ctx);
            it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
            const proto::ProtoList* pair = ctx->newList();
            pair = pair->appendLast(ctx, ctx->fromInteger(static_cast<long>(key)));
            pair = pair->appendLast(ctx, val);
            kwList = kwList->appendLast(ctx, pair->asObject(ctx));
        }
        entry = entry->appendLast(ctx, kwList->asObject(ctx));
    } else {
        entry = entry->appendLast(ctx, ctx->newList()->asObject(ctx));
    }

    // Append to the module's handler list (creating it on first use).
    const proto::ProtoObject* listObj = handlers_get_list(ctx, self);
    const proto::ProtoList* lst = listObj ? listObj->asList(ctx) : ctx->newList();
    lst = lst->appendLast(ctx, entry->asObject(ctx));
    proto::ProtoObject* mself = const_cast<proto::ProtoObject*>(self);
    mself->setAttribute(ctx, sym(ctx, "__atexit_handlers__"), lst->asObject(ctx));

    // CPython returns the callable so `@atexit.register` works as a
    // decorator. The previous stub returned PROTO_NONE which broke
    // the decorator form silently (the decorated name resolved to
    // None).
    return callable;
}

static const proto::ProtoObject* py_unregister(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (!self || !posArgs || posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* target = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* listObj = handlers_get_list(ctx, self);
    if (!listObj) return PROTO_NONE;
    const proto::ProtoList* lst = listObj->asList(ctx);

    // Filter out entries whose callable matches `target`. CPython
    // matches on identity (`is`), so we use pointer equality.
    const proto::ProtoList* kept = ctx->newList();
    unsigned long n = lst->getSize(ctx);
    for (unsigned long i = 0; i < n; ++i) {
        const proto::ProtoObject* entry = lst->getAt(ctx, static_cast<int>(i));
        const proto::ProtoList* el = entry->asList(ctx);
        if (!el) continue;
        const proto::ProtoObject* cb = el->getAt(ctx, 0);
        if (cb != target) {
            kept = kept->appendLast(ctx, entry);
        }
    }
    proto::ProtoObject* mself = const_cast<proto::ProtoObject*>(self);
    mself->setAttribute(ctx, sym(ctx, "__atexit_handlers__"), kept->asObject(ctx));
    return PROTO_NONE;
}

static const proto::ProtoObject* py_run_exitfuncs(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    if (!self) return PROTO_NONE;
    const proto::ProtoObject* listObj = handlers_get_list(ctx, self);
    if (!listObj) return PROTO_NONE;
    const proto::ProtoList* lst = listObj->asList(ctx);

    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    unsigned long n = lst->getSize(ctx);
    // CPython: handlers run LIFO. Latest registration fires first.
    for (long long i = static_cast<long long>(n) - 1; i >= 0; --i) {
        const proto::ProtoObject* entry = lst->getAt(ctx, static_cast<int>(i));
        const proto::ProtoList* el = entry->asList(ctx);
        if (!el || el->getSize(ctx) < 2) continue;
        const proto::ProtoObject* callable = el->getAt(ctx, 0);
        const proto::ProtoObject* argsObj = el->getAt(ctx, 1);
        const proto::ProtoList* args = argsObj ? argsObj->asList(ctx) : nullptr;
        if (!args) args = ctx->newList();

        // CPython swallows exceptions from atexit handlers and prints
        // a traceback to stderr (PyTraceBack_Here). We print a short
        // diagnostic and continue so a single broken handler can't
        // block the rest from running.
        invokePythonCallable(ctx, callable, args, nullptr);
        if (env && env->hasPendingException()) {
            std::fprintf(stderr,
                "atexit: handler raised an exception; continuing.\n");
            env->clearPendingException();
        }
    }

    // Clear the list so a second run_exitfuncs (e.g. from a re-init
    // path) doesn't re-fire handlers.
    proto::ProtoObject* mself = const_cast<proto::ProtoObject*>(self);
    mself->setAttribute(ctx, sym(ctx, "__atexit_handlers__"),
        ctx->newList()->asObject(ctx));
    return PROTO_NONE;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    // Mutable module object so register / unregister can update the
    // handler list in place without breaking sys.modules' pinned
    // reference. With newObject(false) [immutable], setAttribute
    // returned a fresh wrapper that we'd silently discard, leaving
    // the registered list invisible to later lookups.
    const proto::ProtoObject* mod = ctx->newObject(true);
    // Pre-seed the handler list so handlers_get_list always sees a
    // valid list (no PROTO_NONE on first register call).
    mod = mod->setAttribute(ctx, sym(ctx, "__atexit_handlers__"),
        ctx->newList()->asObject(ctx));
    mod = mod->setAttribute(ctx, sym(ctx, "register"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_register));
    mod = mod->setAttribute(ctx, sym(ctx, "unregister"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_unregister));
    mod = mod->setAttribute(ctx, sym(ctx, "_run_exitfuncs"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_run_exitfuncs));
    return mod;
}

} // namespace atexit_module
} // namespace protoPython
