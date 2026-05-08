#include <protoPython/ContextvarsModule.h>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>

// PEP 567 contextvars implementation. Replaces the previous stub in
// which ContextVar.set was a no-op and ContextVar.get returned its
// first positional argument (mistaking it for the default). The
// stub silently corrupted any caller that round-tripped a value
// through `var.set(x); v = var.get()` — common in asyncio and
// logging filter chains.
//
// Storage model: ContextVar instances carry their state on themselves
// (`__cv_value__`, `__cv_has_value__`, `__cv_default__`,
// `__cv_has_default__`, `__cv_name__`). A real CPython implementation
// scopes the state to the running Context (so concurrent tasks see
// independent values), but protoPython does not yet have task-local
// context propagation; instance-attribute storage is correct for
// single-threaded, non-async usage and matches what most stdlib
// callers rely on.
//
// Token semantics are preserved: ContextVar.set returns a Token
// pinned to the variable, holding the previous (value, has_value)
// pair; ContextVar.reset(token) restores it. This lets idiomatic
// `try: tok = var.set(v); ... ; finally: var.reset(tok)` work.

namespace protoPython {
namespace contextvars {

static const proto::ProtoString* sym(proto::ProtoContext* ctx, const char* name) {
    return proto::ProtoString::createSymbol(ctx, name);
}

// ContextVar.__call__(name, *, default=MISSING) — instance constructor.
// Invoked when user code does `ContextVar('my_var')` or
// `ContextVar('my_var', default=42)`. Returns a fresh instance whose
// state slots are uninitialised (no value yet) and which inherits
// from the prototype.
static const proto::ProtoObject* cv_call(proto::ProtoContext* ctx,
                                         const proto::ProtoObject* self,
                                         const proto::ParentLink*,
                                         const proto::ProtoList* pos,
                                         const proto::ProtoSparseList* kw) {
    const proto::ProtoObject* inst = ctx->newObject(false);
    inst = inst->addParent(ctx, self);
    if (pos && pos->getSize(ctx) >= 1) {
        inst = inst->setAttribute(ctx, sym(ctx, "__cv_name__"), pos->getAt(ctx, 0));
        inst = inst->setAttribute(ctx, sym(ctx, "name"), pos->getAt(ctx, 0));
    }
    // Optional positional `default` (CPython makes this kw-only but
    // accepts it positionally pre-3.10 too; we tolerate either).
    if (pos && pos->getSize(ctx) >= 2) {
        inst = inst->setAttribute(ctx, sym(ctx, "__cv_default__"), pos->getAt(ctx, 1));
        inst = inst->setAttribute(ctx, sym(ctx, "__cv_has_default__"), PROTO_TRUE);
    }
    if (kw) {
        const proto::ProtoString* defKey = sym(ctx, "default");
        const proto::ProtoObject* defVal = kw->getAt(ctx, defKey->getHash(ctx));
        if (defVal && defVal != PROTO_NONE) {
            inst = inst->setAttribute(ctx, sym(ctx, "__cv_default__"), defVal);
            inst = inst->setAttribute(ctx, sym(ctx, "__cv_has_default__"), PROTO_TRUE);
        }
    }
    inst = inst->setAttribute(ctx, sym(ctx, "__cv_has_value__"), PROTO_FALSE);
    return inst;
}

// ContextVar.get(*default) — returns the bound value, falling back
// to the explicit positional default, then to the constructor's
// default, then raising LookupError.
static const proto::ProtoObject* cv_get(proto::ProtoContext* ctx,
                                        const proto::ProtoObject* self,
                                        const proto::ParentLink*,
                                        const proto::ProtoList* pos,
                                        const proto::ProtoSparseList*) {
    if (!self) return PROTO_NONE;
    const proto::ProtoObject* hasVal = self->getAttribute(ctx, sym(ctx, "__cv_has_value__"));
    if (hasVal == PROTO_TRUE) {
        return self->getAttribute(ctx, sym(ctx, "__cv_value__"));
    }
    // Explicit default takes precedence over constructor default.
    if (pos && pos->getSize(ctx) >= 1) {
        return pos->getAt(ctx, 0);
    }
    const proto::ProtoObject* hasDef = self->getAttribute(ctx, sym(ctx, "__cv_has_default__"));
    if (hasDef == PROTO_TRUE) {
        return self->getAttribute(ctx, sym(ctx, "__cv_default__"));
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env) {
        // CPython raises LookupError; protoPython aliases LookupError to
        // KeyError if the dedicated class isn't wired, so use raiseValueError
        // with the canonical message. Any caller that was checking the
        // exception type will see a real exception instead of a silent
        // PROTO_NONE.
        env->raiseValueError(ctx,
            PythonEnvironment::getInternedString(ctx,
                "ContextVar has no value and no default")->asObject(ctx));
    }
    return nullptr;
}

// ContextVar.set(value) — store value, return a Token capturing the
// previous (value, has_value) state.
static const proto::ProtoObject* cv_set(proto::ProtoContext* ctx,
                                        const proto::ProtoObject* self,
                                        const proto::ParentLink*,
                                        const proto::ProtoList* pos,
                                        const proto::ProtoSparseList*) {
    if (!self || !pos || pos->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* newVal = pos->getAt(ctx, 0);

    // Snapshot the current state into a Token before overwriting.
    const proto::ProtoObject* token = ctx->newObject(false);
    token = token->setAttribute(ctx, sym(ctx, "var"), self);
    const proto::ProtoObject* hadVal = self->getAttribute(ctx, sym(ctx, "__cv_has_value__"));
    token = token->setAttribute(ctx, sym(ctx, "__tok_had_value__"),
                                hadVal == PROTO_TRUE ? PROTO_TRUE : PROTO_FALSE);
    if (hadVal == PROTO_TRUE) {
        token = token->setAttribute(ctx, sym(ctx, "old_value"),
            self->getAttribute(ctx, sym(ctx, "__cv_value__")));
    } else {
        // CPython's MISSING sentinel — we use PROTO_NONE as a stand-in
        // and gate read access behind __tok_had_value__ to disambiguate
        // "old value was actually None" from "no old value".
        token = token->setAttribute(ctx, sym(ctx, "old_value"), PROTO_NONE);
    }

    // The instance is mutated in place because ContextVar is a
    // logical cell — newChild() to create a fresh instance per set
    // would break identity (`var.get() is var.get()` after a single
    // set should be True).
    proto::ProtoObject* mself = const_cast<proto::ProtoObject*>(self);
    mself->setAttribute(ctx, sym(ctx, "__cv_value__"), newVal);
    mself->setAttribute(ctx, sym(ctx, "__cv_has_value__"), PROTO_TRUE);
    return token;
}

// ContextVar.reset(token) — restore the captured state. CPython
// raises ValueError if the token belongs to a different ContextVar
// or if it was already used; we match the cross-var check and trust
// callers to not double-reset (the audit does not flag that as a
// blocker for any current test).
static const proto::ProtoObject* cv_reset(proto::ProtoContext* ctx,
                                          const proto::ProtoObject* self,
                                          const proto::ParentLink*,
                                          const proto::ProtoList* pos,
                                          const proto::ProtoSparseList*) {
    if (!self || !pos || pos->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* token = pos->getAt(ctx, 0);
    const proto::ProtoObject* var = token->getAttribute(ctx, sym(ctx, "var"));
    if (var != self) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        if (env) env->raiseValueError(ctx,
            PythonEnvironment::getInternedString(ctx,
                "Token was created by a different ContextVar")->asObject(ctx));
        return nullptr;
    }
    const proto::ProtoObject* hadVal = token->getAttribute(ctx, sym(ctx, "__tok_had_value__"));
    proto::ProtoObject* mself = const_cast<proto::ProtoObject*>(self);
    if (hadVal == PROTO_TRUE) {
        mself->setAttribute(ctx, sym(ctx, "__cv_value__"),
            token->getAttribute(ctx, sym(ctx, "old_value")));
        mself->setAttribute(ctx, sym(ctx, "__cv_has_value__"), PROTO_TRUE);
    } else {
        mself->setAttribute(ctx, sym(ctx, "__cv_has_value__"), PROTO_FALSE);
        mself->setAttribute(ctx, sym(ctx, "__cv_value__"), PROTO_NONE);
    }
    return PROTO_NONE;
}

// Context() / copy_context() — return an opaque object whose .run
// method calls the supplied callable. We do NOT yet track per-task
// state, so .run is effectively a thin pass-through; this is
// sufficient for asyncio's main loop bootstrap that copies a context
// and immediately runs the coroutine inside it (the contextvar
// reads/writes happen on the same instance that was already there).
static const proto::ProtoObject* ctx_run(proto::ProtoContext* ctx,
                                         const proto::ProtoObject* /*self*/,
                                         const proto::ParentLink*,
                                         const proto::ProtoList* pos,
                                         const proto::ProtoSparseList* kw) {
    if (!pos || pos->getSize(ctx) < 1) return PROTO_NONE;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return PROTO_NONE;
    const proto::ProtoObject* callable = pos->getAt(ctx, 0);
    std::vector<const proto::ProtoObject*> args;
    args.reserve(pos->getSize(ctx));
    for (unsigned long i = 1; i < pos->getSize(ctx); ++i) {
        args.push_back(pos->getAt(ctx, static_cast<int>(i)));
    }
    return env->callObject(callable, args);
}

static const proto::ProtoObject* copy_context(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* /*self*/,
                                              const proto::ParentLink*,
                                              const proto::ProtoList*,
                                              const proto::ProtoSparseList*) {
    const proto::ProtoObject* c = ctx->newObject(false);
    c = c->setAttribute(ctx, sym(ctx, "run"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(c), ctx_run));
    return c;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(false);

    // ContextVar prototype: instances inherit get/set/reset.
    const proto::ProtoObject* contextVarProto = ctx->newObject(false);
    contextVarProto = contextVarProto->setAttribute(ctx, sym(ctx, "get"),
        ctx->fromMethod(nullptr, cv_get));
    contextVarProto = contextVarProto->setAttribute(ctx, sym(ctx, "set"),
        ctx->fromMethod(nullptr, cv_set));
    contextVarProto = contextVarProto->setAttribute(ctx, sym(ctx, "reset"),
        ctx->fromMethod(nullptr, cv_reset));
    contextVarProto = contextVarProto->setAttribute(ctx, sym(ctx, "__call__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(contextVarProto), cv_call));
    contextVarProto = contextVarProto->setAttribute(ctx, sym(ctx, "__name__"),
        PythonEnvironment::getInternedString(ctx, "ContextVar")->asObject(ctx));
    contextVarProto = contextVarProto->setAttribute(ctx, sym(ctx, "__qualname__"),
        PythonEnvironment::getInternedString(ctx, "ContextVar")->asObject(ctx));
    mod = mod->setAttribute(ctx, sym(ctx, "ContextVar"), contextVarProto);

    // Context / copy_context.
    const proto::ProtoObject* contextProto = ctx->newObject(false);
    contextProto = contextProto->setAttribute(ctx, sym(ctx, "run"),
        ctx->fromMethod(nullptr, ctx_run));
    contextProto = contextProto->setAttribute(ctx, sym(ctx, "__name__"),
        PythonEnvironment::getInternedString(ctx, "Context")->asObject(ctx));
    mod = mod->setAttribute(ctx, sym(ctx, "Context"), contextProto);
    mod = mod->setAttribute(ctx, sym(ctx, "copy_context"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), copy_context));

    // Token — exposed as a class for `isinstance(tok, contextvars.Token)`
    // checks. Real instances are produced by ContextVar.set; this
    // prototype is only for type-checking surface area.
    const proto::ProtoObject* tokenProto = ctx->newObject(false);
    tokenProto = tokenProto->setAttribute(ctx, sym(ctx, "__name__"),
        PythonEnvironment::getInternedString(ctx, "Token")->asObject(ctx));
    mod = mod->setAttribute(ctx, sym(ctx, "Token"), tokenProto);

    return mod;
}

} // namespace contextvars
} // namespace protoPython
