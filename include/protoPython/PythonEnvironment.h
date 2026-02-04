#ifndef PROTOPYTHON_PYTHONENVIRONMENT_H
#define PROTOPYTHON_PYTHONENVIRONMENT_H

#include <protoCore.h>
#include <functional>
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
     * @brief Returns the underlaying ProtoSpace.
     */
    proto::ProtoSpace* getSpace() { return &space; }

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
     * @brief Sets the global trace function (sys.settrace). Thread-safe.
     */
    void setTraceFunction(const proto::ProtoObject* func) {
        std::lock_guard<std::mutex> lock(traceAndExceptionMutex_);
        traceFunction = func;
    }

    /**
     * @brief Gets the global trace function (sys.gettrace). Thread-safe.
     */
    const proto::ProtoObject* getTraceFunction() const {
        std::lock_guard<std::mutex> lock(traceAndExceptionMutex_);
        return traceFunction;
    }

    /**
     * @brief Sets the trace function from sys._trace_default (for --trace CLI).
     */
    void enableDefaultTrace();

    /**
     * @brief Increments sys.stats.calls or sys.stats.objects_created by 1.
     */
    void incrementSysStats(const char* key);

    /**
     * @brief Sets the pending exception (raised by container operations). Thread-safe.
     */
    void setPendingException(const proto::ProtoObject* exc) {
        std::lock_guard<std::mutex> lock(traceAndExceptionMutex_);
        pendingException = exc;
    }

    /**
     * @brief Gets and clears the pending exception. Thread-safe.
     */
    const proto::ProtoObject* takePendingException() {
        std::lock_guard<std::mutex> lock(traceAndExceptionMutex_);
        const proto::ProtoObject* e = pendingException;
        pendingException = nullptr;
        return e;
    }

    /**
     * @brief Returns the environment for the given context (for use by dunder methods).
     */
    static PythonEnvironment* fromContext(proto::ProtoContext* ctx);

    /**
     * @brief Records a KeyError as the pending exception (for container operations).
     */
    void raiseKeyError(proto::ProtoContext* context, const proto::ProtoObject* key);

    /**
     * @brief Records a ValueError as the pending exception (for container operations).
     */
    void raiseValueError(proto::ProtoContext* context, const proto::ProtoObject* msg);

private:
    void initializeRootObjects(const std::string& stdLibPath, const std::vector<std::string>& searchPaths);

    proto::ProtoSpace space;
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
    const proto::ProtoObject* traceFunction{nullptr};
    const proto::ProtoObject* pendingException{nullptr};
    mutable std::mutex traceAndExceptionMutex_;
    std::vector<std::string> argv_;
    int exitRequested_{0};
    ExecutionHook executionHook;
    const proto::ProtoObject* keyErrorType{nullptr};
    const proto::ProtoObject* valueErrorType{nullptr};
    mutable std::unordered_map<std::string, const proto::ProtoObject*> resolveCache_;

    static void registerContext(proto::ProtoContext* ctx, PythonEnvironment* env);
    static void unregisterContext(proto::ProtoContext* ctx);
};

} // namespace protoPython

#endif // PROTOPYTHON_PYTHONENVIRONMENT_H
