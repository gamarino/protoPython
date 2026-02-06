#ifndef PROTOPYTHON_PYTHONENVIRONMENT_H
#define PROTOPYTHON_PYTHONENVIRONMENT_H

#include <protoCore.h>
#include <atomic>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace protoPython {

class PythonEnvironment {
public:
    /**
     * @brief Initializes a new Python environment.
     * @param stdLibPath Path to the Python standard library. If empty, tries defaults.
     * @param searchPaths Module search paths.
     * @param argv Command-line arguments for sys.argv (optional).
     */
    PythonEnvironment(const std::string& stdLibPath = "", const std::vector<std::string>& searchPaths = {},
                     const std::vector<std::string>& argv = {});
    ~PythonEnvironment();

    /**
     * @brief Returns the process singleton ProtoSpace (L-Shape: one per process).
     */
    proto::ProtoSpace* getSpace() { return space_; }

    /** @brief Returns the process singleton ProtoSpace (static accessor). */
    static proto::ProtoSpace* getProcessSpace();

    /**
     * @brief Returns the root context.
     */
    proto::ProtoContext* getContext() { return context; }

    /**
     * @brief Gets the Python 'object' base prototype.
     */
    const proto::ProtoObject* getObjectPrototype() const { return objectPrototype; }

    /**
     * @brief Gets the Python 'type' prototype.
     */
    const proto::ProtoObject* getTypePrototype() const { return typePrototype; }

    /**
     * @brief Gets the Python 'int' prototype.
     */
    const proto::ProtoObject* getIntPrototype() const { return intPrototype; }

    /**
     * @brief Gets the Python 'str' prototype.
     */
    const proto::ProtoObject* getStrPrototype() const { return strPrototype; }

    /**
     * @brief Gets the Python 'list' prototype.
     */
    const proto::ProtoObject* getListPrototype() const { return listPrototype; }

    /**
     * @brief Gets the Python 'dict' prototype.
     */
    const proto::ProtoObject* getDictPrototype() const { return dictPrototype; }

    /**
     * @brief Gets the Python 'tuple' prototype.
     */
    const proto::ProtoObject* getTuplePrototype() const { return tuplePrototype; }

    /**
     * @brief Gets the Python 'set' prototype.
     */
    const proto::ProtoObject* getSetPrototype() const { return setPrototype; }

    /**
     * @brief Gets the Python 'bytes' prototype.
     */
    const proto::ProtoObject* getBytesPrototype() const { return bytesPrototype; }

    /**
     * @brief Gets the Python 'slice' type.
     */
    const proto::ProtoObject* getSliceType() const { return sliceType; }

    /**
     * @brief Utility to resolve symbols in this environment.
     */
    const proto::ProtoObject* resolve(const std::string& name);

    /**
     * @brief Invalidates the import resolution cache (e.g. after module reload).
     */
    void invalidateResolveCache();

    /**
     * @brief Resolves the module by name, then invokes its \c main attribute if it is callable.
     * @param moduleName Module name (as used by resolve).
     * @return 0 on success (including when module has no \c main or it is not callable); non-zero on resolve failure.
     */
    int runModuleMain(const std::string& moduleName);

    /**
     * @brief High-level execution entry: resolve module, invoke pre/post hooks, run main.
     * @param moduleName Module name (as used by resolve).
     * @return 0 on success, -1 on resolve failure, -2 on runtime failure.
     */
    int executeModule(const std::string& moduleName);

    /**
     * @brief Interactive REPL: read lines, eval/exec, print result. Uses builtins.eval/exec/repr.
     * @param in Input stream (default std::cin).
     * @param out Output stream (default std::cout).
     */
    void runRepl(std::istream& in = std::cin, std::ostream& out = std::cout);

    /**
     * @brief Run atexit handlers (resolves atexit._run_exitfuncs and calls it).
     */
    void runExitHandlers();

    /**
     * @brief Execution hook type: (moduleName, phase) where phase 0=before, 1=after.
     */
    using ExecutionHook = std::function<void(const std::string& moduleName, int phase)>;

    /**
     * @brief Sets the optional execution hook (called before and after module execution).
     */
    void setExecutionHook(ExecutionHook hook) { executionHook = std::move(hook); }

    /**
     * @brief Sets sys.argv (list of command-line arguments).
     */
    void setArgv(const std::vector<std::string>& args) { argv_ = args; }

    /**
     * @brief Gets the requested exit code from sys.exit (0 if none).
     */
    int getExitRequested() const { return exitRequested_; }

    /**
     * @brief Sets the exit code requested by sys.exit (for execution engine).
     */
    void setExitRequested(int code) { exitRequested_ = code; }

    /**
     * @brief Sets the input stream for builtins.input(). Defaults to std::cin.
     */
    void setStdin(std::istream* s) { stdin_ = s; }

    /**
     * @brief Gets the input stream for builtins.input().
     */
    std::istream* getStdin() const { return stdin_; }

    /**
     * @brief Sets the trace function for the current thread (sys.settrace). No lock; thread-local.
     */
    void setTraceFunction(const proto::ProtoObject* func);

    /**
     * @brief Gets the trace function for the current thread (sys.gettrace). No lock; thread-local.
     */
    const proto::ProtoObject* getTraceFunction() const;

    /**
     * @brief Sets the trace function from sys._trace_default (for --trace CLI).
     */
    void enableDefaultTrace();

    /**
     * @brief Increments sys.stats.calls or sys.stats.objects_created by 1.
     */
    void incrementSysStats(const char* key);

    /**
     * @brief Sets the pending exception for the current thread. No lock; thread-local.
     */
    void setPendingException(const proto::ProtoObject* exc);

    /**
     * @brief Gets and clears the pending exception for the current thread. No lock; thread-local.
     */
    const proto::ProtoObject* takePendingException();

    /**
     * @brief Returns the environment for the given context (for use by dunder methods).
     */
    static PythonEnvironment* fromContext(proto::ProtoContext* ctx);

    /** Registers a context (e.g. worker thread) with this environment so fromContext() resolves. */
    static void registerContext(proto::ProtoContext* ctx, PythonEnvironment* env);
    /** Unregisters a context when a worker thread exits. */
    static void unregisterContext(proto::ProtoContext* ctx);

    /**
     * @brief Records a KeyError as the pending exception.
     */
    void raiseKeyError(proto::ProtoContext* context, const proto::ProtoObject* key);

    void raiseValueError(proto::ProtoContext* context, const proto::ProtoObject* msg);
    void raiseNameError(proto::ProtoContext* context, const std::string& name);
    void raiseAttributeError(proto::ProtoContext* context, const proto::ProtoObject* obj, const std::string& attr);
    void raiseTypeError(proto::ProtoContext* context, const std::string& msg);
    void raiseImportError(proto::ProtoContext* context, const std::string& msg);
    void raiseKeyboardInterrupt(proto::ProtoContext* context);
    void raiseSyntaxError(proto::ProtoContext* context, const std::string& msg, int lineno, int offset, const std::string& text);
    void raiseSystemExit(proto::ProtoContext* context, int code);
    void raiseRecursionError(proto::ProtoContext* context);
    void raiseZeroDivisionError(proto::ProtoContext* context);

    /**
     * @brief Returns true if this environment is running in interactive mode.
     */
    bool isInteractive() const { return isInteractive_; }

    /**
     * @brief Sets the interactive mode flag (Step 1309).
     */
    void setInteractive(bool interactive) { isInteractive_ = interactive; }

    /**
     * @brief Formats and prints an exception to the given stream (Step 1325).
     */
    void handleException(const proto::ProtoObject* exc, const proto::ProtoObject* frame = nullptr, std::ostream& out = std::cerr);

    /**
     * @brief Implementation of structured exception formatting (Step 1325-1327).
     */
    std::string formatException(const proto::ProtoObject* exc, const proto::ProtoObject* frame = nullptr);

    /**
     * @brief Formats a traceback starting from the given context (Step 1329).
     */
    std::string formatTraceback(const proto::ProtoContext* ctx);

    /**
     * @brief Collects candidate names for fuzzy matching suggestions.
     * @param frame The global frame/scope to collect names from.
     * @param targetObj Optional object to collect attributes from (for AttributeError).
     */
    std::vector<std::string> collectCandidates(const proto::ProtoObject* frame, const proto::ProtoObject* targetObj = nullptr);

private:
    void initializeRootObjects(const std::string& stdLibPath, const std::vector<std::string>& searchPaths);

    /** Helper: checks if a string is a complete Python statement/expression (Step 1307). */
    bool isCompleteBlock(const std::string& code);

    proto::ProtoSpace* space_;
    proto::ProtoContext* context;

    const proto::ProtoObject* objectPrototype;
    const proto::ProtoObject* typePrototype;
    const proto::ProtoObject* intPrototype;
    const proto::ProtoObject* strPrototype;
    const proto::ProtoObject* listPrototype;
    const proto::ProtoObject* dictPrototype;
    const proto::ProtoObject* tuplePrototype;
    const proto::ProtoObject* setPrototype;
    const proto::ProtoObject* bytesPrototype;
    const proto::ProtoObject* sliceType;
    const proto::ProtoObject* frozensetPrototype;
    const proto::ProtoObject* floatPrototype;
    const proto::ProtoObject* boolPrototype;
    const proto::ProtoObject* sysModule;
    const proto::ProtoObject* builtinsModule;
    std::vector<std::string> argv_;
    int exitRequested_{0};
    bool isInteractive_{false};
    std::vector<std::string> replHistory_;
    std::string primaryPrompt_{">>> "};
    std::string secondaryPrompt_{"... "};
    ExecutionHook executionHook;
    const proto::ProtoObject* keyErrorType{nullptr};
    const proto::ProtoObject* valueErrorType{nullptr};
    const proto::ProtoObject* nameErrorType{nullptr};
    const proto::ProtoObject* attributeErrorType{nullptr};
    const proto::ProtoObject* syntaxErrorType{nullptr};
    const proto::ProtoObject* typeErrorType{nullptr};
    const proto::ProtoObject* importErrorType{nullptr};
    const proto::ProtoObject* keyboardInterruptType{nullptr};
    const proto::ProtoObject* systemExitType{nullptr};
    const proto::ProtoObject* recursionErrorType{nullptr};
    const proto::ProtoObject* zeroDivisionErrorType{nullptr};
    /** Incremented on invalidateResolveCache(); per-thread caches check this (lock-free). */
    mutable std::atomic<uint64_t> resolveCacheGeneration_{0};
    std::istream* stdin_{&std::cin};

public:
    /** Signal handling flag (Step 1310). */
    static std::atomic<bool> s_sigintReceived;
};

} // namespace protoPython

#endif // PROTOPYTHON_PYTHONENVIRONMENT_H
