/**
 * The interpreter's debugging hook: the listener, the per-thread switch, the
 * code-object cache the dispatcher's line events read, and the inspection
 * helpers a held thread runs (see include/protoPython/Debugger.h). The
 * dispatcher's side (the activation stack and the per-instruction check) is in
 * ExecutionEngine.cpp.
 */

#include <protoPython/Debugger.h>
#include <protoPython/ExecutionEngine.h>
#include <protoPython/PythonEnvironment.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace protoPython {
namespace debugger {

namespace {

std::atomic<Listener*> g_listener{nullptr};
std::atomic<bool> g_exceptionEvents{false};
thread_local bool tl_enabled = false;
thread_local bool tl_suspended = false;

// The exception that reached a user frame last on this thread, pinned so its
// address is not reused by a new exception while it is remembered.
struct Sightings {
    std::mutex mu;
    PythonEnvironment* env{nullptr};
    proto::ProtoRootSet* roots{nullptr};
};
Sightings& sightings() {
    static Sightings* s = new Sightings();  // never destroyed: used at exit
    return *s;
}
thread_local const proto::ProtoObject* tl_seen = nullptr;
thread_local proto::ProtoRootSet::Handle tl_seenHandle = proto::ProtoRootSet::kNullHandle;
thread_local proto::ProtoRootSet* tl_seenRoots = nullptr;
// Where a library frame saw an exception first (not pinned: only compared).
thread_local const proto::ProtoObject* tl_libSeen = nullptr;
thread_local std::string tl_libOrigin;

void dropSighting() {
    Sightings& s = sightings();
    std::lock_guard<std::mutex> lk(s.mu);
    if (tl_seenHandle != proto::ProtoRootSet::kNullHandle && tl_seenRoots && tl_seenRoots == s.roots)
        s.roots->remove(tl_seenHandle);
    tl_seenHandle = proto::ProtoRootSet::kNullHandle;
    tl_seenRoots = nullptr;
    tl_seen = nullptr;
}

// Code objects seen while debugging, by their co_code tuple (what the
// dispatcher has at hand). The code objects are pinned so a tuple is never
// freed and its address reused while the entry exists.
struct CodeCache {
    std::mutex mu;
    PythonEnvironment* env{nullptr};
    proto::ProtoRootSet* roots{nullptr};
    std::unordered_map<const void*, std::unique_ptr<CodeInfo>> byKey;
    std::string stdlibPrefix;  // normalizePath(stdlib) + '/', for CodeInfo::library
};
CodeCache& codeCache() {
    static CodeCache* cache = new CodeCache();  // never destroyed: used at exit
    return *cache;
}

std::string toUtf8(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    std::string s;
    if (obj && obj != PROTO_NONE && obj->isString(ctx)) obj->asString(ctx)->toUTF8String(ctx, s);
    return s;
}

std::vector<int> decodeLines(proto::ProtoContext* ctx, PythonEnvironment* env,
                             const proto::ProtoObject* codeObj, std::size_t nInstr) {
    // co_lnotab: (instruction delta, signed line delta) pairs relative to
    // co_firstlineno, as Compiler::setLineNumber records them.
    std::vector<int> lines(nInstr, 0);
    int lineno = 0;
    const proto::ProtoObject* fln = codeObj->getAttribute(ctx, env->getCoFirstLinenoString());
    if (fln && fln->isInteger(ctx)) lineno = static_cast<int>(fln->asLong(ctx));
    if (lineno <= 0) return lines;
    const proto::ProtoObject* lnotabObj = codeObj->getAttribute(ctx, env->getCoLnotabString());
    TupleView lnT = (lnotabObj && lnotabObj != PROTO_NONE) ? PythonEnvironment::tupleView(ctx, lnotabObj) : nullptr;
    const proto::ProtoList* lnL = (!lnT && lnotabObj && lnotabObj != PROTO_NONE) ? lnotabObj->asList(ctx) : nullptr;
    const proto::proto_ulong size = lnT ? lnT->getSize(ctx) : (lnL ? lnL->getSize(ctx) : 0);
    auto at = [&](proto::proto_ulong j) -> long long {
        const proto::ProtoObject* o = lnT ? lnT->getAt(ctx, static_cast<int>(j)) : lnL->getAt(ctx, static_cast<int>(j));
        return (o && o->isInteger(ctx)) ? o->asLong(ctx) : 0;
    };
    std::size_t cursor = 0;
    for (proto::proto_ulong j = 0; j + 1 < size; j += 2) {
        const std::size_t pcDelta = static_cast<std::size_t>(at(j));
        const int lineDelta = static_cast<int>(static_cast<signed char>(at(j + 1)));
        const std::size_t until = std::min(cursor + pcDelta, nInstr);
        for (std::size_t k = cursor; k < until; ++k) lines[k] = lineno;
        cursor += pcDelta;
        lineno += lineDelta;
        if (cursor >= nInstr) break;
    }
    for (std::size_t k = cursor; k < nInstr; ++k) lines[k] = lineno;
    return lines;
}

// The Python half of the inspection helpers, compiled once per stop into a
// fresh namespace (see Helpers).
const char* const kHelperSource = R"PY(
def _dap_repr(v):
    try:
        r = repr(v)
    except BaseException as e:
        return '<repr() failed: %s>' % type(e).__name__
    if not isinstance(r, str):
        r = str(r)
    if len(r) > 4000:
        r = r[:4000] + '...'
    return r

def _dap_tname(v):
    try:
        return type(v).__name__
    except BaseException:
        return ''

def _dap_is_module(v):
    try:
        return type(v).__name__ == 'module'
    except BaseException:
        return False

def _dap_expandable(v):
    try:
        if v is None or isinstance(v, (str, bytes, bytearray, bool, int, float, complex)):
            return False
        if isinstance(v, (list, tuple, dict, set, frozenset)):
            return len(v) > 0
        if _dap_is_module(v):
            return True
        if isinstance(v, type) or callable(v):
            return False
        d = getattr(v, '__dict__', None)
        return isinstance(d, dict) and len(d) > 0
    except BaseException:
        return False

_DAP_FRAME_NAMES = frozenset(('f_back', 'f_builtins', 'f_code', 'f_globals', 'f_lasti',
                              'f_lineno', 'f_locals', 'f_trace'))
_DAP_MAPPING_NAMES = frozenset(('clear', 'copy', 'fromkeys', 'get', 'items', 'keys', 'pop',
                                'popitem', 'setdefault', 'update', 'values'))

def _dap_entry(n, v):
    return (n, v, _dap_repr(v), _dap_tname(v), _dap_expandable(v))

def _dap_children(v, plain_names):
    out = []
    try:
        if isinstance(v, dict):
            for k in list(v.keys())[:2000]:
                n = k if (plain_names and isinstance(k, str)) else _dap_repr(k)
                out.append(_dap_entry(n, v[k]))
        elif isinstance(v, (list, tuple)):
            for i in range(min(len(v), 2000)):
                out.append(_dap_entry('[%d]' % i, v[i]))
        elif isinstance(v, (set, frozenset)):
            i = 0
            for x in list(v)[:2000]:
                out.append(_dap_entry('<%d>' % i, x))
                i += 1
        elif _dap_is_module(v):
            for k in sorted(dir(v)):
                if k.startswith('__') and k.endswith('__'):
                    continue
                if k in _DAP_FRAME_NAMES:
                    continue
                try:
                    x = getattr(v, k)
                except BaseException:
                    continue
                # A module namespace answers the mapping methods; they are not
                # the module's variables.
                if k in _DAP_MAPPING_NAMES and _dap_tname(x) == 'builtin_function_or_method':
                    continue
                out.append(_dap_entry(k, x))
        else:
            d = getattr(v, '__dict__', None)
            if isinstance(d, dict):
                for k in sorted(d.keys()):
                    out.append(_dap_entry(k, d[k]))
    except BaseException:
        pass
    return tuple(out)

def _dap_locals(mapping):
    return _dap_children(mapping, True)

def _dap_eval(expr, g, l, allow_exec):
    try:
        v = eval(expr, g, l)
        return (True, v, _dap_repr(v), _dap_tname(v), _dap_expandable(v))
    except SyntaxError as e:
        if not allow_exec:
            return (False, None, 'SyntaxError: %s' % e, '', False)
    except BaseException as e:
        return (False, None, '%s: %s' % (type(e).__name__, e), '', False)
    try:
        exec(expr, g, l)
        return (True, None, '', '', False)
    except BaseException as e:
        return (False, None, '%s: %s' % (type(e).__name__, e), '', False)

def _dap_exc_info(e):
    try:
        t = type(e)
        qn = getattr(t, '__qualname__', None) or getattr(t, '__name__', '?')
        mod = getattr(t, '__module__', None)
        name = qn if mod in (None, '', 'builtins') else '%s.%s' % (mod, qn)
    except BaseException:
        name = '?'
    try:
        msg = str(e)
    except BaseException as x:
        msg = '<str() failed: %s>' % type(x).__name__
    internal = False
    try:
        if isinstance(e, (StopIteration, StopAsyncIteration, GeneratorExit)):
            internal = True
        elif isinstance(e, SystemExit) and e.code in (None, 0):
            internal = True
    except BaseException:
        pass
    return (str(name), msg if isinstance(msg, str) else '', internal)

def _dap_with_suppresses(x, e):
    try:
        s = getattr(x, '__self__', None)
        t = type(s)
        if getattr(t, '__module__', None) == 'contextlib' and t.__name__ == 'suppress':
            return isinstance(e, s._exceptions)
    except BaseException:
        pass
    return False

def _dap_callable_thread_name(c):
    try:
        n = getattr(getattr(c, '__self__', None), 'name', None)
        if isinstance(n, str):
            return n
        n = getattr(c, '__name__', None)
        return n if isinstance(n, str) else ''
    except BaseException:
        return ''

def _dap_current_thread_name():
    try:
        import sys
        th = sys.modules.get('threading')
        if th is None:
            return ''
        t = th._active.get(th.get_ident())
        n = getattr(t, 'name', None)
        return n if isinstance(n, str) else ''
    except BaseException:
        return ''

def _dap_flush():
    import sys
    for s in (sys.stdout, sys.stderr):
        try:
            s.flush()
        except BaseException:
            pass
)PY";

// The helper namespace of the running stop. Built on first use, pinned in
// its own root set for as long as the environment lives (one compile per
// session).
struct Helpers {
    PythonEnvironment* env{nullptr};
    proto::ProtoRootSet* roots{nullptr};
    proto::ProtoRootSet::Handle ns{proto::ProtoRootSet::kNullHandle};
};
Helpers& helpers() {
    static Helpers* h = new Helpers();
    return *h;
}
std::mutex g_helpersMu;

const proto::ProtoObject* builtin(proto::ProtoContext* ctx, PythonEnvironment* env, const char* name) {
    const proto::ProtoObject* b = env->getBuiltins();
    if (!b) return nullptr;
    const proto::ProtoObject* f = b->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, name));
    return (f && f != PROTO_NONE) ? f : nullptr;
}

const proto::ProtoObject* call(proto::ProtoContext* ctx, const proto::ProtoObject* fn,
                               std::initializer_list<const proto::ProtoObject*> args) {
    const proto::ProtoList* list = ctx->newList();
    for (const proto::ProtoObject* a : args) list = list->appendLast(ctx, a ? a : PROTO_NONE);
    return invokePythonCallable(ctx, fn, list, nullptr);
}

// The helper function `name`, or nullptr (an error is reported on stderr once).
const proto::ProtoObject* helper(proto::ProtoContext* ctx, PythonEnvironment* env, const char* name) {
    // Several threads may stop (and inspect) at once. The first one compiles
    // the helpers, allocating: a thread waiting for it here is out of the
    // collector's quorum, or a collection that compile starts never begins.
    std::unique_lock<std::mutex> lk(g_helpersMu, std::try_to_lock);
    if (!lk.owns_lock()) {
        proto::ProtoContext::UnmanagedScope unmanaged(ctx);
        lk.lock();
    }
    Helpers& h = helpers();
    if (h.env != env || !h.roots) {
        h.env = env;
        h.roots = env->getSpace() ? env->getSpace()->createRootSet("protopy-debugger-helpers") : nullptr;
        h.ns = proto::ProtoRootSet::kNullHandle;
        if (!h.roots) return nullptr;
        try {
            const proto::ProtoObject* dictFn = builtin(ctx, env, "dict");
            const proto::ProtoObject* execFn = builtin(ctx, env, "exec");
            if (!dictFn || !execFn) return nullptr;
            const proto::ProtoObject* ns = call(ctx, dictFn, {});
            if (!ns) return nullptr;
            h.ns = h.roots->add(ns);
            call(ctx, execFn, {proto::ProtoString::fromUTF8(ctx, kHelperSource)->asObject(ctx), ns});
        } catch (const proto::ProtoException&) {
            std::fprintf(stderr, "protopy: the debugger's inspection helpers failed to compile\n");
            h.ns = proto::ProtoRootSet::kNullHandle;
            return nullptr;
        }
    }
    if (h.ns == proto::ProtoRootSet::kNullHandle) return nullptr;
    const proto::ProtoObject* ns = h.roots->resolve(h.ns);
    lk.unlock();
    if (!ns) return nullptr;
    try {
        const proto::ProtoObject* f = env->callMethod(ctx, ns, PythonEnvironment::getInternedString(ctx, "__getitem__"),
                                                      {PythonEnvironment::getInternedString(ctx, name)->asObject(ctx)});
        return (f && f != PROTO_NONE) ? f : nullptr;
    } catch (const proto::ProtoException&) {
        return nullptr;
    }
}

// Reads a helper's tuple of (name, value, repr, type, expandable) entries.
void readEntries(proto::ProtoContext* ctx, const proto::ProtoObject* result, std::vector<Value>& out) {
    TupleView t = (result && result != PROTO_NONE) ? PythonEnvironment::tupleView(ctx, result) : nullptr;
    if (!t) return;
    const proto::proto_ulong n = t->getSize(ctx);
    for (proto::proto_ulong i = 0; i < n; ++i) {
        TupleView e = PythonEnvironment::tupleView(ctx, t->getAt(ctx, static_cast<int>(i)));
        if (!e || e->getSize(ctx) < 5) continue;
        Value v;
        v.name = toUtf8(ctx, e->getAt(ctx, 0));
        v.object = e->getAt(ctx, 1);
        v.value = toUtf8(ctx, e->getAt(ctx, 2));
        v.type = toUtf8(ctx, e->getAt(ctx, 3));
        v.expandable = e->getAt(ctx, 4) == PROTO_TRUE;
        out.push_back(std::move(v));
    }
}

bool callEntries(proto::ProtoContext* ctx, PythonEnvironment* env, const char* name,
                 std::initializer_list<const proto::ProtoObject*> args, std::vector<Value>& out) {
    SuspendScope quiet;
    const proto::ProtoObject* fn = helper(ctx, env, name);
    if (!fn) return false;
    try {
        const proto::ProtoObject* r = call(ctx, fn, args);
        PythonEnvironment::TransientPin pin(env, r);
        readEntries(ctx, r, out);
        return true;
    } catch (const proto::ProtoException&) {
        return false;
    }
}

bool isModuleLevel(const StackEntry& e) {
    return e.frame && (e.frame == e.globals || !e.code || !e.code->codeObject);
}

// Calls fn(name, value) for the fast locals of the activation (its context's
// slots: most functions keep their variables there, and have no frame object).
template <typename Fn>
void forEachFastLocal(proto::ProtoContext* ctx, PythonEnvironment* env, const StackEntry& e, Fn&& fn) {
    if (!e.context || !e.code || !e.code->codeObject) return;
    env->forEachFastLocal(ctx, e.code->codeObject, e.context->getAutomaticLocals(),
                          e.context->getAutomaticLocalsCount(), fn);
}

// The local variables as a mapping: the module itself at module level, else a
// dict of the fast locals and of the variables stored on the frame object.
const proto::ProtoObject* localsMapping(proto::ProtoContext* ctx, PythonEnvironment* env, const StackEntry& e) {
    if (isModuleLevel(e)) return e.frame;
    if (e.frame && PythonEnvironment::findLiveFrameContext(e.frame))
        return env->frameLocalsSnapshot(ctx, e.frame);
    const proto::ProtoObject* dictFn = builtin(ctx, env, "dict");
    if (!dictFn) return nullptr;
    const proto::ProtoObject* d = call(ctx, dictFn, {});
    if (!d) return nullptr;
    PythonEnvironment::TransientPin pin(env, d);
    std::vector<std::pair<const proto::ProtoString*, const proto::ProtoObject*>> fast;
    forEachFastLocal(ctx, env, e, [&](const proto::ProtoString* name, const proto::ProtoObject* value) {
        fast.emplace_back(name, value);
    });
    const proto::ProtoString* setItem = PythonEnvironment::getInternedString(ctx, "__setitem__");
    for (const auto& kv : fast) env->callMethod(ctx, d, setItem, {kv.first->asObject(ctx), kv.second});
    return d;
}

// The namespace an expression evaluated "in" the activation sees: at module
// level the module itself; in a function, a new namespace (a child of its frame
// object when it has one) holding the fast locals.
const proto::ProtoObject* evalNamespace(proto::ProtoContext* ctx, PythonEnvironment* env, const StackEntry& e) {
    if (isModuleLevel(e)) return e.frame;
    proto::ProtoObject* ns = const_cast<proto::ProtoObject*>(ctx->newObject(true));
    if (e.frame) ns = const_cast<proto::ProtoObject*>(ns->addParent(ctx, e.frame));
    forEachFastLocal(ctx, env, e, [&](const proto::ProtoString* name, const proto::ProtoObject* value) {
        ns->setAttribute(ctx, name, value);
    });
    return ns;
}

} // namespace

void setListener(Listener* listener) { g_listener.store(listener, std::memory_order_release); }
void enableOnThisThread(bool enable) { tl_enabled = enable; }

SuspendScope::SuspendScope() : saved_(tl_suspended) { tl_suspended = true; }
SuspendScope::~SuspendScope() { tl_suspended = saved_; }

std::string displayPath(const std::string& path) {
    namespace fs = std::filesystem;
    if (path.empty() || path[0] == '<') return path;
    std::error_code ec;
    fs::path p(std::u8string(path.begin(), path.end()));
    fs::path abs = fs::absolute(p, ec);
    if (ec) abs = p;
    abs = abs.lexically_normal();
    abs.make_preferred();
    const std::u8string u = abs.u8string();
    return std::string(u.begin(), u.end());
}

std::string normalizePath(const std::string& path) {
    std::string s = displayPath(path);
    std::replace(s.begin(), s.end(), '\\', '/');
#if defined(_WIN32)
    for (char& c : s)
        if (static_cast<unsigned char>(c) < 0x80) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#endif
    return s;
}

bool detail::listenerInstalled() { return g_listener.load(std::memory_order_relaxed) != nullptr; }

bool detail::threadWantsEvents() {
    return tl_enabled && !tl_suspended && g_listener.load(std::memory_order_relaxed) != nullptr;
}

const CodeInfo* detail::codeInfoFor(proto::ProtoContext* ctx, PythonEnvironment* env,
                                    const proto::ProtoObject* codeObj, const void* key) {
    CodeCache& cache = codeCache();
    {
        std::lock_guard<std::mutex> lk(cache.mu);
        if (cache.env == env) {
            auto it = cache.byKey.find(key);
            if (it != cache.byKey.end()) return it->second.get();
        }
    }
    if (!codeObj || codeObj == PROTO_NONE || !env) return nullptr;
    auto info = std::make_unique<CodeInfo>();
    info->file = toUtf8(ctx, codeObj->getAttribute(ctx, env->getCoFilenameString()));
    if (info->file.empty()) return nullptr;
    info->path = normalizePath(info->file);
    info->displayPath = displayPath(info->file);
    {
        std::string prefix;
        {
            std::lock_guard<std::mutex> lk(cache.mu);
            if (cache.env == env) prefix = cache.stdlibPrefix;
        }
        if (prefix.empty() && !env->getStdLibPath().empty()) prefix = normalizePath(env->getStdLibPath()) + "/";
        const std::string& p = info->path;
        info->library = (!prefix.empty() && p.compare(0, prefix.size(), prefix) == 0)
            || p.find("/site-packages/") != std::string::npos || p.find("/dist-packages/") != std::string::npos
            || p.compare(0, 7, "<frozen") == 0;
    }
    info->name = toUtf8(ctx, codeObj->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "co_qualname")));
    if (info->name.empty()) info->name = toUtf8(ctx, codeObj->getAttribute(ctx, env->getCoNameString()));
    if (info->name.empty()) info->name = "<module>";
    const proto::ProtoObject* co = codeObj->getAttribute(ctx, env->getCoCodeString());
    TupleView bc = (co && co != PROTO_NONE) ? PythonEnvironment::tupleView(ctx, co) : nullptr;
    const std::size_t nInstr = bc ? static_cast<std::size_t>(bc->getSize(ctx) / 2 + 1) : 0;
    info->lines = decodeLines(ctx, env, codeObj, nInstr);

    std::lock_guard<std::mutex> lk(cache.mu);
    if (cache.env != env) {
        cache.byKey.clear();
        cache.env = env;
        cache.roots = env->getSpace() ? env->getSpace()->createRootSet("protopy-debugger-code") : nullptr;
        cache.stdlibPrefix = env->getStdLibPath().empty() ? std::string() : normalizePath(env->getStdLibPath()) + "/";
    }
    auto it = cache.byKey.find(key);
    if (it != cache.byKey.end()) return it->second.get();
    if (cache.roots) cache.roots->add(codeObj);
    info->codeObject = codeObj;
    const CodeInfo* result = info.get();
    cache.byKey.emplace(key, std::move(info));
    return result;
}

void detail::dispatchLine(proto::ProtoContext* ctx, PythonEnvironment* env, const CodeInfo& code, int line,
                          LineEventKind kind, std::size_t depth) {
    Listener* l = g_listener.load(std::memory_order_acquire);
    if (!l || tl_suspended) return;
    SuspendScope quiet;
    l->onLine(ctx, env, code, line, kind, depth);
}

bool detail::exceptionEventsWanted() {
    return g_exceptionEvents.load(std::memory_order_relaxed) && threadWantsEvents();
}

bool detail::firstSighting(proto::ProtoContext* ctx, PythonEnvironment* env, const proto::ProtoObject* exception) {
    (void)ctx;
    if (!exception) return false;
    if (exception == tl_seen) return false;
    dropSighting();
    Sightings& s = sightings();
    std::lock_guard<std::mutex> lk(s.mu);
    if (s.env != env || !s.roots) {
        s.env = env;
        s.roots = env && env->getSpace() ? env->getSpace()->createRootSet("protopy-debugger-exceptions") : nullptr;
    }
    tl_seen = exception;
    if (s.roots) {
        tl_seenRoots = s.roots;
        tl_seenHandle = s.roots->add(exception);
    }
    return true;
}

void detail::forgetSighting() {
    if (tl_seen) dropSighting();
    tl_libSeen = nullptr;
}

void detail::noteLibrarySighting(const proto::ProtoObject* exception, const CodeInfo& code, int line) {
    if (exception == tl_libSeen || exception == tl_seen) return;
    tl_libSeen = exception;
    std::string file = code.displayPath;
    const auto slash = file.find_last_of("/\\");
    if (slash != std::string::npos) file = file.substr(slash + 1);
    tl_libOrigin = code.name + " (" + file + ":" + std::to_string(line) + ")";
}

std::string detail::librarySightingOrigin(const proto::ProtoObject* exception) {
    return exception && exception == tl_libSeen ? tl_libOrigin : std::string();
}

void detail::dispatchException(proto::ProtoContext* ctx, PythonEnvironment* env, const ExceptionEvent& event,
                               std::size_t depth) {
    Listener* l = g_listener.load(std::memory_order_acquire);
    if (!l || tl_suspended) return;
    SuspendScope quiet;
    l->onException(ctx, env, event, depth);
}

void setExceptionEvents(bool enable) { g_exceptionEvents.store(enable, std::memory_order_release); }

ThreadScope::ThreadScope(proto::ProtoContext* ctx, PythonEnvironment* env, const proto::ProtoObject* callable) {
    Listener* l = g_listener.load(std::memory_order_acquire);
    if (!l || !env) return;
    std::string name;
    {
        SuspendScope quiet;
        if (const proto::ProtoObject* fn = helper(ctx, env, "_dap_callable_thread_name")) {
            try {
                name = toUtf8(ctx, call(ctx, fn, {callable}));
            } catch (const proto::ProtoException&) {
            }
        }
        listener_ = l;
        l->onThreadStart(ctx, env, name);
    }
    tl_enabled = true;
}

ThreadScope::~ThreadScope() {
    if (!listener_) return;
    tl_enabled = false;
    dropSighting();
    tl_libSeen = nullptr;
    // The session may have ended (and removed its listener) meanwhile; the
    // listener object itself outlives every thread (DAPServer).
    listener_->onThreadExit();
}

bool describeException(proto::ProtoContext* ctx, PythonEnvironment* env, const proto::ProtoObject* exception,
                       ExceptionDescription& out) {
    SuspendScope quiet;
    const proto::ProtoObject* fn = helper(ctx, env, "_dap_exc_info");
    if (!fn) return false;
    try {
        const proto::ProtoObject* r = call(ctx, fn, {exception});
        PythonEnvironment::TransientPin pin(env, r);
        TupleView t = (r && r != PROTO_NONE) ? PythonEnvironment::tupleView(ctx, r) : nullptr;
        if (!t || t->getSize(ctx) < 3) return false;
        out.typeName = toUtf8(ctx, t->getAt(ctx, 0));
        out.message = toUtf8(ctx, t->getAt(ctx, 1));
        out.internal = t->getAt(ctx, 2) == PROTO_TRUE;
        return true;
    } catch (const proto::ProtoException&) {
        return false;
    }
}

bool withSuppresses(proto::ProtoContext* ctx, PythonEnvironment* env, const proto::ProtoObject* exitMethod,
                    const proto::ProtoObject* exception) {
    if (!exitMethod || exitMethod == PROTO_NONE) return false;
    SuspendScope quiet;
    const proto::ProtoObject* fn = helper(ctx, env, "_dap_with_suppresses");
    if (!fn) return false;
    try {
        return call(ctx, fn, {exitMethod, exception}) == PROTO_TRUE;
    } catch (const proto::ProtoException&) {
        return false;
    }
}

std::string currentThreadName(proto::ProtoContext* ctx, PythonEnvironment* env) {
    SuspendScope quiet;
    const proto::ProtoObject* fn = helper(ctx, env, "_dap_current_thread_name");
    if (!fn) return std::string();
    try {
        return toUtf8(ctx, call(ctx, fn, {}));
    } catch (const proto::ProtoException&) {
        return std::string();
    }
}

void detail::environmentClosing(PythonEnvironment* env) {
    {
        Sightings& s = sightings();
        std::lock_guard<std::mutex> lk(s.mu);
        if (s.env == env) {
            if (s.roots && env->getSpace()) env->getSpace()->destroyRootSet(s.roots);
            s.roots = nullptr;
            s.env = nullptr;
        }
    }
    {
        CodeCache& cache = codeCache();
        std::lock_guard<std::mutex> lk(cache.mu);
        if (cache.env == env) {
            if (cache.roots && env->getSpace()) env->getSpace()->destroyRootSet(cache.roots);
            cache.roots = nullptr;
            cache.byKey.clear();
            cache.env = nullptr;
        }
    }
    std::lock_guard<std::mutex> lk(g_helpersMu);
    Helpers& h = helpers();
    if (h.env == env) {
        if (h.roots && env->getSpace()) env->getSpace()->destroyRootSet(h.roots);
        h.roots = nullptr;
        h.ns = proto::ProtoRootSet::kNullHandle;
        h.env = nullptr;
    }
}

bool frameLocals(proto::ProtoContext* ctx, PythonEnvironment* env, const StackEntry& frame,
                 std::vector<Value>& out) {
    SuspendScope quiet;
    const proto::ProtoObject* mapping = nullptr;
    try {
        mapping = localsMapping(ctx, env, frame);
    } catch (const proto::ProtoException&) {
        return false;
    }
    PythonEnvironment::TransientPin pin(env, mapping);
    return mapping && callEntries(ctx, env, "_dap_locals", {mapping}, out);
}

bool frameGlobals(proto::ProtoContext* ctx, PythonEnvironment* env, const StackEntry& frame,
                  std::vector<Value>& out) {
    return frame.globals && callEntries(ctx, env, "_dap_locals", {frame.globals}, out);
}

bool children(proto::ProtoContext* ctx, PythonEnvironment* env, const proto::ProtoObject* object,
              std::vector<Value>& out) {
    return object && callEntries(ctx, env, "_dap_children", {object, PROTO_FALSE}, out);
}

bool evaluate(proto::ProtoContext* ctx, PythonEnvironment* env, const StackEntry& frame,
              const std::string& expression, bool allowExec, Value& out, std::string& error) {
    SuspendScope quiet;
    if (!frame.globals) { error = "no frame"; return false; }
    const proto::ProtoObject* fn = helper(ctx, env, "_dap_eval");
    if (!fn) { error = "the debugger's helpers are not available"; return false; }
    try {
        const proto::ProtoObject* globals = frame.globals;
        const proto::ProtoObject* ns = evalNamespace(ctx, env, frame);
        PythonEnvironment::TransientPin pinNs(env, ns);
        const proto::ProtoObject* expr = proto::ProtoString::fromUTF8(ctx, expression.c_str())->asObject(ctx);
        const proto::ProtoObject* r = call(ctx, fn, {expr, globals, ns, allowExec ? PROTO_TRUE : PROTO_FALSE});
        PythonEnvironment::TransientPin pin(env, r);
        TupleView t = (r && r != PROTO_NONE) ? PythonEnvironment::tupleView(ctx, r) : nullptr;
        if (!t || t->getSize(ctx) < 5) { error = "evaluation failed"; return false; }
        if (t->getAt(ctx, 0) != PROTO_TRUE) {
            error = toUtf8(ctx, t->getAt(ctx, 2));
            return false;
        }
        out.object = t->getAt(ctx, 1);
        out.value = toUtf8(ctx, t->getAt(ctx, 2));
        out.type = toUtf8(ctx, t->getAt(ctx, 3));
        out.expandable = t->getAt(ctx, 4) == PROTO_TRUE;
        return true;
    } catch (const proto::ProtoException&) {
        error = "evaluation raised an exception";
        return false;
    }
}

void flushStandardStreams(proto::ProtoContext* ctx, PythonEnvironment* env) {
    SuspendScope quiet;
    const proto::ProtoObject* fn = helper(ctx, env, "_dap_flush");
    if (fn) {
        try { call(ctx, fn, {}); } catch (const proto::ProtoException&) {}
    }
    std::fflush(stdout);
    std::fflush(stderr);
}

Pins::~Pins() { release(); }

std::size_t Pins::add(PythonEnvironment* env, const proto::ProtoObject* object) {
    if (!roots_ && env && env->getSpace()) {
        space_ = env->getSpace();
        roots_ = space_->createRootSet("protopy-debugger-values");
    }
    handles_.push_back(roots_ && object ? roots_->add(object) : proto::ProtoRootSet::kNullHandle);
    return handles_.size() - 1;
}

const proto::ProtoObject* Pins::get(std::size_t index) const {
    if (!roots_ || index >= handles_.size() || handles_[index] == proto::ProtoRootSet::kNullHandle) return nullptr;
    return roots_->resolve(handles_[index]);
}

void Pins::release() {
    if (roots_ && space_) space_->destroyRootSet(roots_);
    roots_ = nullptr;
    space_ = nullptr;
    handles_.clear();
}

} // namespace debugger
} // namespace protoPython
