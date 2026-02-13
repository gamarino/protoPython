#ifndef PROTOPYTHON_PYTHONENVIRONMENT_H
#define PROTOPYTHON_PYTHONENVIRONMENT_H

#include <protoCore.h>
#include <atomic>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <protoPython/Tokenizer.h>

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
    proto::ProtoContext* getContext() { return rootContext_; }

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
     * @brief Gets the Python 'None' prototype.
     */
    const proto::ProtoObject* getNonePrototype() const { return nonePrototype; }
    /** @brief Gets the frame prototype. */
    const proto::ProtoObject* getFramePrototype() const { return framePrototype; }
    /** @brief Gets the generator prototype. */
    const proto::ProtoObject* getGeneratorPrototype() const { return generatorPrototype; }

    const proto::ProtoObject* getGlobals() const;

    /** @brief Gets the builtins module object. */
    const proto::ProtoObject* getBuiltins() const { return builtinsModule; }

    /**
     * @brief Utility to resolve symbols in this environment.
     */
    const proto::ProtoObject* resolve(const std::string& name, proto::ProtoContext* ctx = nullptr);
    bool isResolved(const std::string& name, proto::ProtoContext* ctx = nullptr);
    
    /**
     * @brief Runs the event loop until the given coroutine is complete.
     */
    const proto::ProtoObject* runUntilComplete(const proto::ProtoObject* coro);

    /**
     * @brief Schedules a coroutine task to be run by the event loop.
     */
    void addTask(const proto::ProtoObject* coro);

    /**
     * @brief Accessors for frequently used dunder strings (performance).
     */
    const proto::ProtoString* getIterString() const { return iterString; }
    const proto::ProtoString* getNextString() const { return nextString; }
    const proto::ProtoList* getEmptyList() const { return emptyList; }

    const proto::ProtoString* getRangeCurString() const { return rangeCurString; }
    const proto::ProtoString* getRangeStopString() const { return rangeStopString; }
    const proto::ProtoString* getRangeStepString() const { return rangeStepString; }
    const proto::ProtoString* getMapFuncString() const { return mapFuncString; }
    const proto::ProtoString* getMapIterString() const { return mapIterString; }
    const proto::ProtoString* getEnumIterString() const { return enumIterString; }
    const proto::ProtoString* getEnumIdxString() const { return enumIdxString; }
    const proto::ProtoString* getRevObjString() const { return revObjString; }
    const proto::ProtoString* getRevIdxString() const { return revIdxString; }
    const proto::ProtoString* getZipItersString() const { return zipItersString; }
    const proto::ProtoString* getFilterFuncString() const { return filterFuncString; }
    const proto::ProtoString* getFilterIterString() const { return filterIterString; }
    const proto::ProtoString* getEnterString() const { return enterString; }
    const proto::ProtoString* getExitString() const { return exitString; }

    const proto::ProtoString* getClassString() const { return classString; }
    const proto::ProtoString* getNameString() const { return nameString; }
    const proto::ProtoString* getCallString() const { return callString; }
    const proto::ProtoString* getGetItemString() const { return getItemString; }
    const proto::ProtoString* getSetItemString() const { return setItemString; }
    const proto::ProtoString* getDelItemString() const { return delItemString; }

    const proto::ProtoString* getLenString() const { return lenString; }
    const proto::ProtoString* getBoolString() const { return boolString; }
    const proto::ProtoString* getIntString() const { return intString; }
    const proto::ProtoString* getFloatString() const { return floatString; }
    const proto::ProtoString* getStrString() const { return strString; }
    const proto::ProtoString* getReprString() const { return reprString; }
    const proto::ProtoString* getHashString() const { return hashString; }
    const proto::ProtoString* getContainsString() const { return containsString; }
    const proto::ProtoString* getFormatString() const { return formatString; }
    const proto::ProtoString* getDictDunderString() const { return dictString; }
    const proto::ProtoString* getDocString() const { return docString; }
    const proto::ProtoString* getReversedString() const { return reversedString; }
    const proto::ProtoString* getGetDunderString() const { return getDunderString; }
    const proto::ProtoString* getSetDunderString() const { return setDunderString; }
    const proto::ProtoString* getDelDunderString() const { return delDunderString; }
    const proto::ProtoString* getEqString() const { return py_eq_s; }
    const proto::ProtoString* getNeString() const { return py_ne_s; }
    const proto::ProtoString* getLtString() const { return py_lt_s; }
    const proto::ProtoString* getLeString() const { return py_le_s; }
    const proto::ProtoString* getGtString() const { return py_gt_s; }
    const proto::ProtoString* getGeString() const { return py_ge_s; }

    const proto::ProtoObject* getAttribute(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const proto::ProtoString* name);
    const proto::ProtoObject* setAttribute(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const proto::ProtoString* name, const proto::ProtoObject* value);

    const proto::ProtoObject* compareObjects(proto::ProtoContext* ctx, const proto::ProtoObject* a, const proto::ProtoObject* b, int op);
    bool objectsEqual(proto::ProtoContext* ctx, const proto::ProtoObject* a, const proto::ProtoObject* b);

    // Helpers for C++ generated code
    static PythonEnvironment* get(proto::ProtoContext* ctx) { return fromContext(ctx); }
    const proto::ProtoObject* binaryOp(const proto::ProtoObject* a, TokenType op, const proto::ProtoObject* b);
    const proto::ProtoObject* unaryOp(TokenType op, const proto::ProtoObject* a);
    const proto::ProtoObject* iter(const proto::ProtoObject* obj);
    const proto::ProtoObject* next(const proto::ProtoObject* obj);
    void raiseException(const proto::ProtoObject* exc);
    void raiseImportError(const std::string& msg);
    bool isException(const proto::ProtoObject* exc, const proto::ProtoObject* type);
    const proto::ProtoObject* lookupName(const std::string& name);
    void storeName(const std::string& name, const proto::ProtoObject* val);
    void augAssignName(const std::string& name, TokenType op, const proto::ProtoObject* value);
    void augAssignAttr(const proto::ProtoObject* obj, const std::string& attr, TokenType op, const proto::ProtoObject* value);
    void augAssignItem(const proto::ProtoObject* container, const proto::ProtoObject* key, TokenType op, const proto::ProtoObject* value);
    const proto::ProtoObject* callObject(const proto::ProtoObject* callable, const std::vector<const proto::ProtoObject*>& args);
    const proto::ProtoObject* callObjectEx(const proto::ProtoObject* callable, 
                                          const std::vector<const proto::ProtoObject*>& args,
                                          const std::vector<std::pair<std::string, const proto::ProtoObject*>>& keywords,
                                          const proto::ProtoObject* starargs = nullptr,
                                          const proto::ProtoObject* kwargs = nullptr);
    const proto::ProtoObject* buildString(const proto::ProtoObject** parts, size_t count);
    const proto::ProtoObject* getItem(const proto::ProtoObject* container, const proto::ProtoObject* key);
    void setItem(const proto::ProtoObject* container, const proto::ProtoObject* key, const proto::ProtoObject* value);
    const proto::ProtoObject* getAttr(const proto::ProtoObject* obj, const std::string& attr);
    void setAttr(const proto::ProtoObject* obj, const std::string& attr, const proto::ProtoObject* val);
    bool isTrue(const proto::ProtoObject* obj);
    const proto::ProtoObject* importModule(const std::string& name, int level = 0, const std::vector<std::string>& fromList = {});
    void importStar(const proto::ProtoObject* mod);

    const proto::ProtoObject* buildSlice(const proto::ProtoObject* start, const proto::ProtoObject* stop, const proto::ProtoObject* step);
    void delItem(const proto::ProtoObject* container, const proto::ProtoObject* key);
    void delAttr(const proto::ProtoObject* obj, const std::string& attr);
    void delName(const std::string& name);

    const proto::ProtoString* getEnumProtoString() const { return enumProtoS; }
    const proto::ProtoString* getRevProtoString() const { return revProtoS; }
    const proto::ProtoString* getZipProtoString() const { return zipProtoS; }
    const proto::ProtoString* getFilterProtoString() const { return filterProtoS; }
    const proto::ProtoString* getMapProtoString() const { return mapProtoS; }
    const proto::ProtoString* getRangeProtoString() const { return rangeProtoS; }
    const proto::ProtoString* getBoolTypeString() const { return boolTypeS; }
    const proto::ProtoString* getFilterBoolString() const { return filterBoolS; }
    const proto::ProtoObject* getImportErrorType() const { return importErrorType; }
    const proto::ProtoObject* getAttributeErrorType() const { return attributeErrorType; }
    const proto::ProtoObject* getNameErrorType() const { return nameErrorType; }
    const proto::ProtoObject* getZeroInteger() const { return zeroInteger; }
    const proto::ProtoObject* getOneInteger() const { return oneInteger; }

    const proto::ProtoString* getCodeString() const { return __code__; }
    const proto::ProtoString* getGlobalsString() const { return __globals__; }
    const proto::ProtoString* getCoVarnamesString() const { return co_varnames; }
    const proto::ProtoString* getCoNparamsString() const { return co_nparams; }
    const proto::ProtoString* getCoAutomaticCountString() const { return co_automatic_count; }
    const proto::ProtoString* getCoIsGeneratorString() const { return co_is_generator; }
    const proto::ProtoString* getCoFlagsString() const { return co_flags; }
    const proto::ProtoString* getCoConstsString() const { return co_consts; }
    const proto::ProtoString* getCoNamesString() const { return co_names; }
    const proto::ProtoString* getCoCodeString() const { return co_code; }
    const proto::ProtoString* getSendString() const { return sendString; }
    const proto::ProtoString* getThrowString() const { return throwString; }
    const proto::ProtoString* getCloseString() const { return closeString; }
    const proto::ProtoString* getSelfDunderString() const { return selfDunder; }
    const proto::ProtoString* getFuncDunderString() const { return funcDunder; }
    const proto::ProtoString* getFBackString() const { return f_back; }
    const proto::ProtoString* getFCodeString() const { return f_code; }
    const proto::ProtoString* getFGlobalsString() const { return f_globals; }
    const proto::ProtoString* getFLocalsString() const { return f_locals; }
    const proto::ProtoString* getClosureString() const { return __closure__; }

    const proto::ProtoString* getGiCodeString() const { return gi_code; }
    const proto::ProtoString* getGiFrameString() const { return gi_frame; }
    const proto::ProtoString* getGiRunningString() const { return gi_running; }
    const proto::ProtoString* getGiYieldFromString() const { return gi_yieldfrom; }
    const proto::ProtoString* getGiPCString() const { return gi_pc; }
    const proto::ProtoString* getGiStackString() const { return gi_stack; }
    const proto::ProtoString* getGiLocalsString() const { return gi_locals; }

    const proto::ProtoString* getIAddString() const { return __iadd__; }
    const proto::ProtoString* getISubString() const { return __isub__; }
    const proto::ProtoString* getIMulString() const { return __imul__; }
    const proto::ProtoString* getITrueDivString() const { return __itruediv__; }
    const proto::ProtoString* getIFloorDivString() const { return __ifloordiv__; }
    const proto::ProtoString* getIModString() const { return __imod__; }
    const proto::ProtoString* getIPowString() const { return __ipow__; }
    const proto::ProtoString* getILShiftString() const { return __ilshift__; }
    const proto::ProtoString* getIRShiftString() const { return __irshift__; }
    const proto::ProtoString* getIAndString() const { return __iand__; }
    const proto::ProtoString* getIOrString() const { return __ior__; }
    const proto::ProtoString* getIXorString() const { return __ixor__; }
    const proto::ProtoString* getAwaitString() const { return awaitString; }
    const proto::ProtoString* getAIterString() const { return aiterString; }
    const proto::ProtoString* getANextString() const { return anextString; }
    const proto::ProtoString* getAEnterString() const { return aenterString; }
    const proto::ProtoString* getAExitString() const { return aexitString; }
    const proto::ProtoString* getGiNativeCallbackString() const { return giNativeCallbackString; }

    const proto::ProtoString* getAndString() const { return __and__; }
    const proto::ProtoString* getRAndString() const { return __rand__; }
    const proto::ProtoString* getOrString() const { return __or__; }
    const proto::ProtoString* getROrString() const { return __ror__; }
    const proto::ProtoString* getXorString() const { return __xor__; }
    const proto::ProtoString* getRXorString() const { return __rxor__; }

    const proto::ProtoString* getInvertString() const { return __invert__; }
    const proto::ProtoString* getPosString() const { return __pos__; }

    const proto::ProtoString* getDataString() const { return dataString; }
    const proto::ProtoString* getKeysString() const { return keysString; }
    const proto::ProtoString* getInitString() const { return initString; }

    const proto::ProtoString* getStartString() const { return startString; }
    const proto::ProtoString* getStopString() const { return stopString; }
    const proto::ProtoString* getStepString() const { return stepString; }

    const proto::ProtoString* getIOModuleString() const { return ioModuleString; }
    const proto::ProtoString* getOpenString() const { return openString; }

    const proto::ProtoString* getListTypeString() const { return listS; }
    const proto::ProtoString* getDictTypeString() const { return dictS; }
    const proto::ProtoString* getTupleTypeString() const { return tupleS; }
    const proto::ProtoString* getSetTypeString() const { return setS; }
    const proto::ProtoString* getIntTypeNameString() const { return intS; }
    const proto::ProtoString* getFloatTypeNameString() const { return floatS; }
    const proto::ProtoString* getStrTypeNameString() const { return strS; }
    const proto::ProtoString* getBoolTypeNameString() const { return boolS; }
    const proto::ProtoString* getObjectString() const { return objectS; }
    const proto::ProtoString* getTypeString() const { return typeS; }

    /**
     * @brief Invalidates the import resolution cache (e.g. after module reload).
     */
    static PythonEnvironment* getCurrentEnvironment();
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
    int executeModule(const std::string& moduleName, bool asMain = false, proto::ProtoContext* ctx = nullptr);
    
    /**
     * @brief Executes a string of Python code in the current environment's __main__ context.
     * @param source The source code to execute.
     * @param name The name of the source (e.g. "<string>").
     * @return 0 on success, -2 on runtime failure.
     */
    int executeString(const std::string& source, const std::string& name = "<string>");

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
     * @brief Sets the current execution frame for the current thread.
     */
    static void setCurrentFrame(const proto::ProtoObject* frame);

    /**
     * @brief Gets the current execution frame for the current thread.
     */
    static const proto::ProtoObject* getCurrentFrame();

    /**
     * @brief Sets the current globals for the current thread.
     */
    static void setCurrentGlobals(const proto::ProtoObject* globals);

    /**
     * @brief Gets the current globals for the current thread.
     */
    static const proto::ProtoObject* getCurrentGlobals();

    /**
     * @brief Sets the current code object for the current thread.
     */
    static void setCurrentCodeObject(const proto::ProtoObject* code);

    /**
     * @brief Gets the current code object for the current thread.
     */
    static const proto::ProtoObject* getCurrentCodeObject();
    
    /** Sets the current thread-local context (for RAII management). */
    static void setCurrentContext(proto::ProtoContext* ctx) { s_threadContext = ctx; }
    /** Gets the current thread-local context. */
    static proto::ProtoContext* getCurrentContext() { return s_threadContext; }

    /**
     * @brief Returns true if there is a pending exception.
     */
    bool hasPendingException() const;

    /**
     * @brief Returns the pending exception without clearing it.
     */
    const proto::ProtoObject* peekPendingException() const;

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
    void raiseEOFError(proto::ProtoContext* ctx);
    void raiseRecursionError(proto::ProtoContext* context);
    void raiseAssertionError(proto::ProtoContext* ctx, const proto::ProtoObject* msg = nullptr);
    void raiseZeroDivisionError(proto::ProtoContext* ctx);
    void raiseIndexError(proto::ProtoContext* context, const std::string& msg);
    void raiseStopIteration(proto::ProtoContext* context, const proto::ProtoObject* value = nullptr);
    void raiseStopAsyncIteration(proto::ProtoContext* context);

    /**
     * @brief Returns true if the object is a StopIteration exception.
     */
    bool isStopIteration(const proto::ProtoObject* exc) const;

    /**
     * @brief Extracts the return value from a StopIteration exception.
     */
    const proto::ProtoObject* getStopIterationValue(proto::ProtoContext* ctx, const proto::ProtoObject* exc) const;

    /**
     * @brief Clears the pending exception.
     */
    void clearPendingException();

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
    
    // Keyword Names Stack management (for **kwargs refinement)
    void pushKwNames(const proto::ProtoTuple* names);
    void popKwNames();
    const proto::ProtoTuple* getCurrentKwNames() const;

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
    proto::ProtoContext* rootContext_;

    const proto::ProtoObject* objectPrototype;
    const proto::ProtoObject* typePrototype;
    const proto::ProtoObject* intPrototype;
    const proto::ProtoObject* strPrototype;
    const proto::ProtoObject* listPrototype;
    const proto::ProtoObject* dictPrototype;
    const proto::ProtoObject* tuplePrototype;
    const proto::ProtoObject* setPrototype;
    const proto::ProtoObject* bytesPrototype;
    const proto::ProtoObject* nonePrototype;
    const proto::ProtoObject* framePrototype;
    const proto::ProtoObject* generatorPrototype;
    const proto::ProtoObject* sliceType;
    const proto::ProtoObject* frozensetPrototype;
    const proto::ProtoObject* floatPrototype;
    const proto::ProtoObject* boolPrototype;
    const proto::ProtoObject* sysModule;
    static thread_local PythonEnvironment* s_threadEnv;
    static thread_local proto::ProtoContext* s_threadContext;
    static thread_local const proto::ProtoObject* s_currentFrame;
    static thread_local const proto::ProtoObject* s_currentGlobals;
    static thread_local const proto::ProtoObject* s_currentCodeObject;
    const proto::ProtoObject* builtinsModule;
    std::vector<std::string> argv_;
    int exitRequested_{0};
    bool isInteractive_{false};
    std::vector<std::string> replHistory_;
    std::string primaryPrompt_{">>> "};
    std::string secondaryPrompt_{"... "};
    ExecutionHook executionHook;
    const proto::ProtoObject* exceptionType{nullptr};
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
    const proto::ProtoObject* stopIterationType = nullptr;
    const proto::ProtoObject* eofErrorType = nullptr;
    const proto::ProtoObject* assertionErrorType = nullptr;
    const proto::ProtoObject* zeroDivisionErrorType = nullptr;
    const proto::ProtoObject* indexErrorType{nullptr};
    const proto::ProtoObject* stopAsyncIterationType{nullptr};
    const proto::ProtoList* taskQueue{nullptr};
    const proto::ProtoString* iterString{nullptr};
    const proto::ProtoString* nextString{nullptr};
    const proto::ProtoList* emptyList{nullptr};

    const proto::ProtoString* rangeCurString{nullptr};
    const proto::ProtoString* rangeStopString{nullptr};
    const proto::ProtoString* rangeStepString{nullptr};
    const proto::ProtoString* mapFuncString{nullptr};
    const proto::ProtoString* mapIterString{nullptr};
    const proto::ProtoString* enumIterString{nullptr};
    const proto::ProtoString* enumIdxString{nullptr};
    const proto::ProtoString* revObjString{nullptr};
    const proto::ProtoString* revIdxString{nullptr};
    const proto::ProtoString* zipItersString{nullptr};
    const proto::ProtoString* filterFuncString{nullptr};
    const proto::ProtoString* filterIterString{nullptr};
    const proto::ProtoString* enterString{nullptr};
    const proto::ProtoString* exitString{nullptr};

    const proto::ProtoString* classString{nullptr};
    const proto::ProtoString* nameString{nullptr};
    const proto::ProtoString* callString{nullptr};
    const proto::ProtoString* getItemString{nullptr};

    const proto::ProtoString* lenString{nullptr};
    const proto::ProtoString* boolString{nullptr};
    const proto::ProtoString* intString{nullptr};
    const proto::ProtoString* floatString{nullptr};
    const proto::ProtoString* strString{nullptr};
    const proto::ProtoString* reprString{nullptr};
    const proto::ProtoString* hashString{nullptr};
    const proto::ProtoString* powString{nullptr};
    const proto::ProtoString* containsString{nullptr};
    const proto::ProtoString* addString{nullptr};
    const proto::ProtoString* formatString{nullptr};
    const proto::ProtoString* dictString{nullptr};
    const proto::ProtoString* docString{nullptr};
    const proto::ProtoString* reversedString{nullptr};
    
    // Keyword Names Stack (thread-local per Environment)
    std::vector<const proto::ProtoTuple*> kwNamesStack;
    const proto::ProtoString* getDunderString{nullptr};
    const proto::ProtoString* setDunderString{nullptr};
    const proto::ProtoString* delDunderString{nullptr};
    const proto::ProtoString* __closure__{nullptr};

    const proto::ProtoString* enumProtoS{nullptr};
    const proto::ProtoString* revProtoS{nullptr};
    const proto::ProtoString* zipProtoS{nullptr};
    const proto::ProtoString* filterProtoS{nullptr};
    const proto::ProtoString* mapProtoS{nullptr};
    const proto::ProtoString* rangeProtoS{nullptr};
    const proto::ProtoString* boolTypeS{nullptr};
    const proto::ProtoString* filterBoolS{nullptr};

    const proto::ProtoString* __code__{nullptr};
    const proto::ProtoString* __globals__{nullptr};
    const proto::ProtoString* co_varnames{nullptr};
    const proto::ProtoString* co_nparams{nullptr};
    const proto::ProtoString* co_automatic_count{nullptr};
    const proto::ProtoString* co_is_generator{nullptr};
    const proto::ProtoString* co_flags{nullptr};
    const proto::ProtoString* co_consts{nullptr};
    const proto::ProtoString* co_names{nullptr};
    const proto::ProtoString* co_code{nullptr};
    const proto::ProtoString* giNativeCallbackString{nullptr};
    const proto::ProtoString* sendString{nullptr};
    const proto::ProtoString* throwString{nullptr};
    const proto::ProtoString* closeString{nullptr};
    const proto::ProtoString* selfDunder{nullptr};
    const proto::ProtoString* funcDunder{nullptr};
    const proto::ProtoString* f_back{nullptr};
    const proto::ProtoString* f_code{nullptr};
    const proto::ProtoString* f_globals{nullptr};
    const proto::ProtoString* f_locals{nullptr};
    const proto::ProtoString* gi_code{nullptr};
    const proto::ProtoString* gi_frame{nullptr};
    const proto::ProtoString* gi_running{nullptr};
    const proto::ProtoString* gi_yieldfrom{nullptr};
    const proto::ProtoString* gi_pc{nullptr};
    const proto::ProtoString* gi_stack{nullptr};
    const proto::ProtoString* gi_locals{nullptr};

    const proto::ProtoString* __iadd__{nullptr};
    const proto::ProtoString* __isub__{nullptr};
    const proto::ProtoString* __imul__{nullptr};
    const proto::ProtoString* __itruediv__{nullptr};
    const proto::ProtoString* __ifloordiv__{nullptr};
    const proto::ProtoString* __imod__{nullptr};
    const proto::ProtoString* __ipow__{nullptr};
    const proto::ProtoString* __ilshift__{nullptr};
    const proto::ProtoString* __irshift__{nullptr};
    const proto::ProtoString* __iand__{nullptr};
    const proto::ProtoString* __ior__{nullptr};
    const proto::ProtoString* __ixor__{nullptr};

    const proto::ProtoString* __and__{nullptr};
    const proto::ProtoString* __rand__{nullptr};
    const proto::ProtoString* __or__{nullptr};
    const proto::ProtoString* __ror__{nullptr};
    const proto::ProtoString* __xor__{nullptr};
    const proto::ProtoString* __rxor__{nullptr};

    const proto::ProtoString* __invert__{nullptr};
    const proto::ProtoString* __pos__{nullptr};
    const proto::ProtoString* awaitString{nullptr};
    const proto::ProtoString* aiterString{nullptr};
    const proto::ProtoString* anextString{nullptr};
    const proto::ProtoString* aenterString{nullptr};
    const proto::ProtoString* aexitString{nullptr};

    const proto::ProtoString* setItemString{nullptr};
    const proto::ProtoString* delItemString{nullptr};
    const proto::ProtoString* dataString{nullptr};
    const proto::ProtoString* keysString{nullptr};
    const proto::ProtoString* initString{nullptr};

    const proto::ProtoString* startString{nullptr};
    const proto::ProtoString* stopString{nullptr};
    const proto::ProtoString* stepString{nullptr};

    const proto::ProtoString* ioModuleString{nullptr};
    const proto::ProtoString* openString{nullptr};

    const proto::ProtoObject* zeroInteger{nullptr};
    const proto::ProtoObject* oneInteger{nullptr};

    const proto::ProtoString* listS{nullptr};
    const proto::ProtoString* dictS{nullptr};
    const proto::ProtoString* tupleS{nullptr};
    const proto::ProtoString* setS{nullptr};
    const proto::ProtoString* intS{nullptr};
    const proto::ProtoString* floatS{nullptr};
    const proto::ProtoString* strS{nullptr};
    const proto::ProtoString* boolS{nullptr};
    const proto::ProtoString* objectS{nullptr};
    const proto::ProtoString* typeS{nullptr};
    const proto::ProtoString* py_eq_s{nullptr};
    const proto::ProtoString* py_ne_s{nullptr};
    const proto::ProtoString* py_lt_s{nullptr};
    const proto::ProtoString* py_le_s{nullptr};
    const proto::ProtoString* py_gt_s{nullptr};
    const proto::ProtoString* py_ge_s{nullptr};

    /** Incremented on invalidateResolveCache(); per-thread caches check this (lock-free). */
    mutable std::atomic<uint64_t> resolveCacheGeneration_{0};
    std::istream* stdin_{&std::cin};

public:
    /** Signal handling flag (Step 1310). */
    static std::atomic<bool> s_sigintReceived;
    static std::thread::id s_mainThreadId;
    mutable std::recursive_mutex importLock_;

    /** RAII scope for managing thread-local Python environment and context registration. */
    class ContextScope {
    public:
        ContextScope(PythonEnvironment* env, proto::ProtoContext* ctx) : ctx_(ctx) {
            prevEnv_ = PythonEnvironment::s_threadEnv;
            prevCtx_ = PythonEnvironment::s_threadContext;
            PythonEnvironment::registerContext(ctx, env);
        }
        ~ContextScope() {
            if (std::getenv("PROTO_THREAD_DIAG")) std::cerr << "[proto-thread] ContextScope destruction ctx=" << ctx_ << " tid=" << std::this_thread::get_id() << "\n" << std::flush;
            PythonEnvironment::s_threadEnv = prevEnv_;
            PythonEnvironment::s_threadContext = prevCtx_;
        }
    private:
        proto::ProtoContext* ctx_;
        PythonEnvironment* prevEnv_;
        proto::ProtoContext* prevCtx_;
    };

    /** RAII lock for importLock_ that is GC-aware (parks thread while waiting). */
    class SafeImportLock {
    public:
        SafeImportLock(PythonEnvironment* env, proto::ProtoContext* ctx);
        ~SafeImportLock();
    private:
        PythonEnvironment* env_;
        proto::ProtoContext* ctx_;
    };
};

} // namespace protoPython

#endif // PROTOPYTHON_PYTHONENVIRONMENT_H
