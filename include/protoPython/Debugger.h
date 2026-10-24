#ifndef PROTOPYTHON_DEBUGGER_H
#define PROTOPYTHON_DEBUGGER_H

/**
 * @file Debugger.h
 * @brief The interpreter's source-level debugging hook (used by `protopy --dap`).
 *
 * The bytecode dispatcher reports a *line event* to the installed Listener
 * whenever a thread that enabled debugging (the session's main thread, and the
 * Python threads started while a listener is installed: ThreadScope) starts
 * executing a new source line (and, for stepping, when a call it made returns).
 * With exception events on, it also reports an *exception event* when an
 * exception first reaches a frame of the user's code (ExceptionEvent). Nothing
 * is reported, and the dispatcher pays one predictable branch per instruction,
 * while no listener is installed or on threads that did not enable it.
 *
 * Everything here is called on the thread that runs the Python code, with its
 * own context; the listener may run Python code there (to inspect values) while
 * it holds the thread at a line event. Code run that way is not traced.
 */

#include <protoCore.h>

#include <cstddef>
#include <functional>
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
    /** Standard library, site-packages or frozen code: not the user's. Exception
     *  breakpoints look at the user's frames only (see ExceptionEvent). */
    bool library{false};
};

/** A Python exception reaching a frame of the user's code for the first time
 *  (the innermost one it crosses: the raising frame, or the frame that called
 *  the library or native code that raised it). Reported while that frame and
 *  all its callers are intact, before any of them handles it. */
struct ExceptionEvent {
    const proto::ProtoObject* exception;
    /** Empty when the frame's own `raise` raised it; else where it came from
     *  ("native code", or the library function and line). */
    std::string origin;
    /** Whether a try/except of the user's frames on this thread would handle
     *  it: walks the active try blocks, innermost first, up to the first frame
     *  of library code, and tests each except clause's type. Runs Python code
     *  (the clauses' type expressions); call it at most once. */
    std::function<bool()> handledInUserCode;
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
    /** An exception reached the user's code (only while setExceptionEvents(true)). */
    virtual void onException(proto::ProtoContext* ctx, PythonEnvironment* env, const ExceptionEvent& event,
                             std::size_t depth) {
        (void)ctx; (void)env; (void)event; (void)depth;
    }
    /** A Python thread (threading.Thread, _thread.start_new_thread) starts, on
     *  that thread, before it runs Python code; its events are enabled. */
    virtual void onThreadStart(proto::ProtoContext* ctx, PythonEnvironment* env, const std::string& name) {
        (void)ctx; (void)env; (void)name;
    }
    /** That thread's Python code finished (on the thread). */
    virtual void onThreadExit() {}
};

/** Installs (or with nullptr removes) the listener. */
void setListener(Listener* listener);
/** Line events of the calling thread go to the listener (default: off). */
void enableOnThisThread(bool enable);
/** Exception events go to the listener (default: off; then a raise costs nothing more). */
void setExceptionEvents(bool enable);

/** The debugger's side of a Python thread's life: enables its events and tells
 *  the listener, when one is installed (ThreadModule's thread bootstrap). */
class ThreadScope {
public:
    ThreadScope(proto::ProtoContext* ctx, PythonEnvironment* env, const proto::ProtoObject* callable);
    ~ThreadScope();
    ThreadScope(const ThreadScope&) = delete;
    ThreadScope& operator=(const ThreadScope&) = delete;
private:
    Listener* listener_{nullptr};
};

/** What the client is told about an exception. */
struct ExceptionDescription {
    std::string typeName;  ///< ValueError, mymodule.MyError
    std::string message;   ///< str(exception)
    /** Iteration protocol and normal exit: StopIteration, StopAsyncIteration,
     *  GeneratorExit, SystemExit with code None or 0. Never a stop. */
    bool internal{false};
};
bool describeException(proto::ProtoContext* ctx, PythonEnvironment* env, const proto::ProtoObject* exception,
                       ExceptionDescription& out);
/** Whether the bound __exit__ of a with block suppresses `exception` without
 *  calling it: true for a contextlib.suppress whose types match. */
bool withSuppresses(proto::ProtoContext* ctx, PythonEnvironment* env, const proto::ProtoObject* exitMethod,
                    const proto::ProtoObject* exception);
/** threading's name of the calling thread (Thread.name may change after it
 *  started), or "" when threading does not know it. */
std::string currentThreadName(proto::ProtoContext* ctx, PythonEnvironment* env);

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
/** Exception events are on and this thread reports events. */
bool exceptionEventsWanted();
/** True the first time `exception` reaches a user frame on this thread (it is
 *  remembered, pinned, until another one does or forgetSighting()). */
bool firstSighting(proto::ProtoContext* ctx, PythonEnvironment* env, const proto::ProtoObject* exception);
/** An explicit `raise x`: x reaching a user frame again is a new sighting. */
void forgetSighting();
/** A library frame saw `exception` before any user frame: remembered (not
 *  pinned) for the event's origin. */
void noteLibrarySighting(const proto::ProtoObject* exception, const CodeInfo& code, int line);
/** Where a library frame saw `exception` first, or "". */
std::string librarySightingOrigin(const proto::ProtoObject* exception);
void dispatchException(proto::ProtoContext* ctx, PythonEnvironment* env, const ExceptionEvent& event,
                       std::size_t depth);
/** Called by ~PythonEnvironment: drops the code cache pinned in its space. */
void environmentClosing(PythonEnvironment* env);
} // namespace detail

} // namespace debugger
} // namespace protoPython

#endif
