#ifndef PROTOPYTHON_DEBUGGER_H
#define PROTOPYTHON_DEBUGGER_H

/**
 * @file Debugger.h
 * @brief The interpreter's source-level debugging hook (used by `protopy --dap`).
 *
 * The bytecode dispatcher reports a *line event* to the installed Listener
 * whenever the thread that enabled debugging starts executing a new source line
 * (and, for stepping, when a call it made returns). Nothing is reported, and the
 * dispatcher pays one predictable branch per instruction, while no listener is
 * installed or on threads that did not enable it.
 *
 * Everything here is called on the thread that runs the Python code, with its
 * own context; the listener may run Python code there (to inspect values) while
 * it holds the thread at a line event. Code run that way is not traced.
 */

#include <protoCore.h>

#include <cstddef>
#include <string>
#include <vector>

namespace protoPython {

class PythonEnvironment;

namespace debugger {

/** What the debugger knows about one code object (built once, cached). */
struct CodeInfo {
    std::string file;        ///< co_filename as the compiler recorded it
    std::string path;        ///< normalizePath(file): the key breakpoints use
    std::string displayPath; ///< absolute, native separators: what a client opens
    std::string name;        ///< co_qualname (co_name when absent)
    std::vector<int> lines;  ///< source line of each instruction (0 = unknown)
    const proto::ProtoObject* codeObject{nullptr};  ///< pinned while cached
};

enum class LineEventKind {
    Line,        ///< the thread starts a new source line
    AfterReturn  ///< a call made from this line returned (same line continues)
};

/** One running Python activation of the debugged thread. */
struct StackEntry {
    const proto::ProtoObject* frame;   ///< its frame object: a module's namespace at module
                                       ///< level, nullptr for most function calls
    const proto::ProtoObject* globals; ///< the namespace its global names resolve in
    proto::ProtoContext* context;      ///< the context whose slots hold its fast locals
    const CodeInfo* code;
    int line;
};

/** Receives line events. Installed process-wide; events come only from threads
 *  that called enableOnThisThread(true). */
class Listener {
public:
    virtual ~Listener() = default;
    /** `depth` is the number of Python activations on the thread (1 = outermost). */
    virtual void onLine(proto::ProtoContext* ctx, PythonEnvironment* env, const CodeInfo& code,
                        int line, LineEventKind kind, std::size_t depth) = 0;
};

/** Installs (or with nullptr removes) the listener. */
void setListener(Listener* listener);
/** Line events of the calling thread go to the listener (default: off). */
void enableOnThisThread(bool enable);

/** No line events on this thread while it lives (the listener's own Python calls). */
class SuspendScope {
public:
    SuspendScope();
    ~SuspendScope();
    SuspendScope(const SuspendScope&) = delete;
    SuspendScope& operator=(const SuspendScope&) = delete;
private:
    bool saved_;
};

/** The calling thread's Python activations, innermost first. Only meaningful
 *  from inside Listener::onLine. */
void currentStack(std::vector<StackEntry>& out);

/** A file path as breakpoints are matched: absolute, normalized, '/' separators,
 *  and on Windows lower case. */
std::string normalizePath(const std::string& path);
/** Absolute, normalized, with the platform's separators. */
std::string displayPath(const std::string& path);

/** A value shown to the client. `object` is only valid while the thread is held. */
struct Value {
    std::string name;
    std::string value;   ///< repr()
    std::string type;    ///< type(value).__name__
    const proto::ProtoObject* object{nullptr};
    bool expandable{false};
};

/** Inspection, for the thread held in Listener::onLine (its own ctx). Each
 *  catches the Python exceptions it causes. */
bool frameLocals(proto::ProtoContext* ctx, PythonEnvironment* env, const StackEntry& frame,
                 std::vector<Value>& out);
bool frameGlobals(proto::ProtoContext* ctx, PythonEnvironment* env, const StackEntry& frame,
                  std::vector<Value>& out);
bool children(proto::ProtoContext* ctx, PythonEnvironment* env, const proto::ProtoObject* object,
              std::vector<Value>& out);
/** eval(expression) in the frame's namespace; with `allowExec` a statement is
 *  exec'd when it is not an expression. On failure `error` holds "Type: message". */
bool evaluate(proto::ProtoContext* ctx, PythonEnvironment* env, const StackEntry& frame,
              const std::string& expression, bool allowExec, Value& out, std::string& error);
/** sys.stdout.flush() and sys.stderr.flush(), errors ignored. */
void flushStandardStreams(proto::ProtoContext* ctx, PythonEnvironment* env);

/** Pins objects (values the client may expand) until release(). */
class Pins {
public:
    Pins() = default;
    ~Pins();
    Pins(const Pins&) = delete;
    Pins& operator=(const Pins&) = delete;
    /** Index of the pinned object, for get(). */
    std::size_t add(PythonEnvironment* env, const proto::ProtoObject* object);
    const proto::ProtoObject* get(std::size_t index) const;
    void release();
private:
    proto::ProtoSpace* space_{nullptr};
    proto::ProtoRootSet* roots_{nullptr};
    std::vector<proto::ProtoRootSet::Handle> handles_;
};

namespace detail {
// Used by the dispatcher (ExecutionEngine.cpp) and PythonEnvironment.
bool threadWantsEvents();
bool listenerInstalled();
const CodeInfo* codeInfoFor(proto::ProtoContext* ctx, PythonEnvironment* env,
                            const proto::ProtoObject* codeObj, const void* key);
void dispatchLine(proto::ProtoContext* ctx, PythonEnvironment* env, const CodeInfo& code, int line,
                  LineEventKind kind, std::size_t depth);
/** Called by ~PythonEnvironment: drops the code cache pinned in its space. */
void environmentClosing(PythonEnvironment* env);
} // namespace detail

} // namespace debugger
} // namespace protoPython

#endif
