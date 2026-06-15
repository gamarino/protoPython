#ifndef PROTOPYTHON_PYTHONENVIRONMENT_H
#define PROTOPYTHON_PYTHONENVIRONMENT_H

#include <protoCore.h>
#include <protoPython/DiagUtils.h>
#include <atomic>
#include <functional>
#include <iostream>
#include <mutex>
#include <mutex>
#include <unordered_map>
#include <thread>
#include <unordered_map>
#include <vector>
#include <protoPython/Tokenizer.h>

namespace protoPython {
class NativeModuleProvider;

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

    // Global string interning for resolving cross-module identity matches
    static const proto::ProtoString* getInternedString(proto::ProtoContext* ctx, const std::string& str);
    
    /**
     * @brief Returns a canonical dunder string for the given name.
     */
    static const proto::ProtoString* getInternalString(proto::ProtoContext* ctx, const char* name);

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
     * @brief Wraps a raw ProtoList* into a Python `list` instance.
     *
     * Native handlers that build a ProtoList with appendLast and
     * return `protoList->asObject(ctx)` directly produce a value
     * for which Python's `type()` reports "list" but `__len__` /
     * `__bool__` resolve to 0/False — the slot dispatchers walk
     * `self.__data__`, and a bare ProtoList has no `__data__`
     * attribute.  This helper produces a real list instance
     * (parent = listPrototype, `__data__` = the raw list).
     *
     * Always prefer this over `rawList->asObject(ctx)` at every
     * return-to-Python boundary.  Static so handlers that don't
     * already hold a `PythonEnvironment*` can call it directly via
     * `PythonEnvironment::wrapList(ctx, list)`; falls back to
     * `rawList->asObject(ctx)` if the environment is not yet
     * initialised (e.g. bootstrap).
     */
    static const proto::ProtoObject* wrapList(proto::ProtoContext* ctx,
                                              const proto::ProtoList* rawList);

    /**
     * @brief Gets the Python 'dict' prototype.
     */

    /**
     * @brief Gets the Python 'tuple' prototype.
     */

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
    /** @brief Gets the Python 'Ellipsis' prototype. */
    const proto::ProtoObject* getEllipsisPrototype() const { return ellipsisPrototype; }
    /** @brief Gets the Python 'NotImplemented' prototype. */
    const proto::ProtoObject* getNotImplementedPrototype() const { return notImplementedPrototype; }
    /** @brief Gets the Python 'NoneType' prototype. */
    const proto::ProtoObject* getNoneTypePrototype() const { return noneTypeProto; }
    /** @brief Gets the frame prototype. */
    const proto::ProtoObject* getFramePrototype() const { return framePrototype; }
    /** @brief Gets the generator prototype. */
    const proto::ProtoObject* getGeneratorPrototype() const { return generatorPrototype; }
    /** @brief Gets the coroutine prototype (async def, no yield). */
    const proto::ProtoObject* getCoroutinePrototype() const { return coroutinePrototype; }
    /** @brief Gets the async_generator prototype (async def with yield). */
    const proto::ProtoObject* getAsyncGeneratorPrototype() const { return asyncGeneratorPrototype; }
    /** @brief Gets the function prototype. */
    const proto::ProtoObject* getFunctionPrototype() const { return functionPrototype; }
    /** @brief Gets the frozenset prototype. */
    const proto::ProtoObject* getFrozensetPrototype() const { return frozensetPrototype; }
    /** @brief Gets the float prototype. */
    const proto::ProtoObject* getFloatPrototype() const { return floatPrototype; }
    /** @brief Gets the complex prototype. */
    const proto::ProtoObject* getComplexPrototype() const { return complexPrototype; }
    /** @brief Gets the bool prototype. */
    const proto::ProtoObject* getBoolPrototype() const { return boolPrototype; }
    /** @brief Gets the module prototype. */
    const proto::ProtoObject* getModulePrototype() const { return modulePrototype; }
    /** @brief Gets the mappingproxy prototype. */
    const proto::ProtoObject* getMappingProxyPrototype() const { return mappingProxyPrototype; }
    /** @brief Gets the method prototype. */
    const proto::ProtoObject* getMethodPrototype() const { return methodPrototype; }
    /** @brief Gets the getset_descriptor prototype. */
    const proto::ProtoObject* getGetSetDescriptorPrototype() const { return getSetDescriptorPrototype; }
    /** @brief Gets the `super` type prototype (a real subclassable type). */
    const proto::ProtoObject* getSuperPrototype() const { return superPrototype; }
    void setSuperPrototype(const proto::ProtoObject* p) { superPrototype = p; }

    void setObjectPrototype(const proto::ProtoObject* p) { objectPrototype = p; }
    void setTypePrototype(const proto::ProtoObject* p) { typePrototype = p; }
    void setDictPrototype(const proto::ProtoObject* p) { dictPrototype = p; }
    /** @brief Gets the path to the standard library. */
    const std::string& getStdLibPath() const { return stdLibPath_; }

    const proto::ProtoObject* getGlobals() const;

    /** @brief Gets the builtins module object. */
    const proto::ProtoObject* getBuiltins() const { return builtinsModule; }
    const proto::ProtoObject* getGeneratorExitType() const { return generatorExitType; }
    const proto::ProtoObject* getStopIterationType() const { return stopIterationType; }

    /** @brief Gets the sys module object. */
    const proto::ProtoObject* getSysModule() const { return sysModule; }
    
    /** @brief Ensures a module is registered in sys.modules. */
    void ensureModuleInSysModules(proto::ProtoContext* ctx, const std::string& name, const proto::ProtoObject* mod);

    /** @brief Internal registration logic for sys.modules. */
    static void registerInSysModules(proto::ProtoContext* ctx, const proto::ProtoObject* sysModule, const std::string& name, const proto::ProtoObject* mod);

    /**
     * @brief Utility to resolve symbols in this environment.
     */
    const proto::ProtoObject* resolve(const std::string& name, proto::ProtoContext* ctx = nullptr);
    const proto::ProtoObject* resolve(const proto::ProtoString* name, proto::ProtoContext* ctx = nullptr);
    bool isResolved(const std::string& name, proto::ProtoContext* ctx = nullptr);

    static inline bool is_missing(const proto::ProtoObject* obj) {
        return obj == nullptr || obj == PROTO_NONE;
    }

    static inline bool attribute_exists(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const proto::ProtoString* name, const proto::ProtoObject* val) {
        if (val != PROTO_NONE) return val != nullptr;
        if (!obj || !name) return false;
        return obj->hasAttribute(ctx, name) == PROTO_TRUE;
    }
    
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
    const proto::ProtoSparseList* getEmptySparseList() const { return emptySparseList; }

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
    const proto::ProtoString* getNewString() const { return newString; }
    const proto::ProtoString* getPrepareString() const { return prepareString; }
    const proto::ProtoString* getMroString() const { return mroString; }
    const proto::ProtoString* getBasesString() const { return basesString; }
    const proto::ProtoString* getExecutedString() const { return executedString; }
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
    const proto::ProtoString* getMatMulString() const { return matMulString; }
    const proto::ProtoString* getIMatMulString() const { return imatmulString; }
    const proto::ProtoString* getRMatMulString() const { return rmatmulString; }
    const proto::ProtoString* getModuleString() const { return moduleString; }
    const proto::ProtoString* getBuiltinsString() const { return builtinsString; }
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
    const proto::ProtoString* getPathS() const { return pathS; }
    const proto::ProtoString* getPathDunderS() const { return pathDunderS; }
    const proto::ProtoString* getFileDunderS() const { return fileDunderS; }
    const proto::ProtoString* getModulesS() const { return modulesS; }
    const proto::ProtoObject* getGenericAliasProto() const { return genericAliasProto; }
    void setGenericAliasProto(const proto::ProtoObject* p) { genericAliasProto = p; }
    const proto::ProtoObject* getUnionTypeProto() const { return unionTypeProto; }
    void setUnionTypeProto(const proto::ProtoObject* p) { unionTypeProto = p; }

    // When `outIsUnboundFunc` is non-null, this becomes a LOAD_METHOD-style
    // lookup: for plain Python functions found on a class (not on instance
    // dict, not a property/classmethod/etc.), binding is deferred to the
    // caller and `*outIsUnboundFunc` is set to true.  In that case the
    // caller should invoke the result as `result(obj, *args)`.  In all
    // other cases `*outIsUnboundFunc` is left/set to false and the
    // returned value is bound (or simply the resolved attribute) ready
    // to call as `result(*args)`.  This avoids `py_function_get`'s ~10-
    // cell bound-method object on the LOAD_METHOD/CALL hot path, where
    // the bound method is created and then immediately consumed.
    const proto::ProtoObject* getAttribute(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const proto::ProtoString* name, bool raiseError = true, bool* outIsUnboundFunc = nullptr);
    static const proto::ProtoObject* py_function_get(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs);
    static const proto::ProtoObject* py_method_call(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs);
    static const proto::ProtoObject* py_method_repr(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs);
    static const proto::ProtoObject* py_method_new(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs);

    const proto::ProtoObject* getType(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

    bool isActuallyAClass(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

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
    void addTraceback(const proto::ProtoObject* exc, const proto::ProtoObject* frame, int lasti, int lineno);
    void raiseImportError(const std::string& msg);
    bool isException(const proto::ProtoObject* exc, const proto::ProtoObject* type);
    const proto::ProtoObject* lookupName(const std::string& name);
    void storeName(const std::string& name, const proto::ProtoObject* val);
    void augAssignName(const std::string& name, TokenType op, const proto::ProtoObject* value);
    void augAssignAttr(const proto::ProtoObject* obj, const std::string& attr, TokenType op, const proto::ProtoObject* value);
    void augAssignItem(const proto::ProtoObject* container, const proto::ProtoObject* key, TokenType op, const proto::ProtoObject* value);
    const proto::ProtoObject* callObject(const proto::ProtoObject* callable, const std::vector<const proto::ProtoObject*>& args);
    /**
     * Method-call helper used by protopyc-compiled code for `obj.attr(args...)`.
     *
     * Uses LOAD_METHOD-style attribute resolution: when the attribute is a
     * plain function reachable through `obj`'s prototype chain, the raw
     * function is returned and `self` is prepended to `args` here, mirroring
     * what the bytecode interpreter's LOAD_METHOD + CALL pair does.  For
     * already-bound callables (Python bound methods carrying `__self__`,
     * staticmethod, classmethod, callable instances …) the resolved value
     * is invoked as-is with `args` unchanged.
     *
     * This is the entry point compiled call-sites need so that user-defined
     * methods see `self` in `args[0]` exactly as their prologue expects.
     */
    const proto::ProtoObject* callMethod(const proto::ProtoObject* obj, const std::string& attr, const std::vector<const proto::ProtoObject*>& args);
    const proto::ProtoObject* callObjectEx(const proto::ProtoObject* callable, 
                                          const std::vector<const proto::ProtoObject*>& args,
                                          const std::vector<std::pair<std::string, const proto::ProtoObject*>>& keywords,
                                          const proto::ProtoObject* starargs = nullptr,
                                          const proto::ProtoObject* kwargs = nullptr);
    const proto::ProtoObject* buildString(const proto::ProtoObject** parts, size_t count);
    // Convenience overload for the protopyc-emitted call form, which
    // uses an initializer-list / std::vector of pointers — the same
    // shape callObject takes.  Forwards to the pointer-array variant
    // via a stable local copy because vector::data() returns
    // `const T* const*` and the pointer-array variant expects
    // `const T**`.
    const proto::ProtoObject* buildString(const std::vector<const proto::ProtoObject*>& parts) {
        if (parts.empty()) return buildString(nullptr, 0);
        std::vector<const proto::ProtoObject*> copy(parts);
        return buildString(copy.data(), copy.size());
    }
    const proto::ProtoObject* getItem(const proto::ProtoObject* container, const proto::ProtoObject* key, proto::ProtoContext* ctx = nullptr);
    void setItem(const proto::ProtoObject* container, const proto::ProtoObject* key, const proto::ProtoObject* value, proto::ProtoContext* ctx = nullptr);
    const proto::ProtoObject* initDictStorage(proto::ProtoContext* ctx, const proto::ProtoObject* obj);
    const proto::ProtoObject* getAttr(const proto::ProtoObject* obj, const std::string& attr);
    void setAttr(const proto::ProtoObject* obj, const std::string& attr, const proto::ProtoObject* val);
    bool isTrue(const proto::ProtoObject* obj);
    const proto::ProtoObject* resolveModule(const std::string& nameStr, proto::ProtoContext* ctx = nullptr);
    const proto::ProtoObject* importModule(const std::string& name, int level = 0, const std::vector<std::string>& fromList = {});
    void importStar(const proto::ProtoObject* mod);

    const proto::ProtoObject* buildSlice(const proto::ProtoObject* start, const proto::ProtoObject* stop, const proto::ProtoObject* step);
    const proto::ProtoObject* newTuple(const proto::ProtoList* list);
    const proto::ProtoObject* newList(const proto::ProtoList* list);
    void delItem(const proto::ProtoObject* container, const proto::ProtoObject* key, proto::ProtoContext* ctx = nullptr);
    void delAttr(const proto::ProtoObject* obj, const std::string& attr);
    void delName(const std::string& name);

    const proto::ProtoString* getEnumProtoString() const { return enumProtoS; }
    const proto::ProtoString* getRevProtoString() const { return revProtoS; }
    const proto::ProtoString* getZipProtoString() const { return zipProtoS; }
    
    const proto::ProtoString* getCoFilenameString() const { return coFilenameString; }
    const proto::ProtoString* getCoFirstLinenoString() const { return coFirstLinenoString; }
    const proto::ProtoString* getCoLnotabString() const { return coLnotabString; }
    const proto::ProtoString* getFilterProtoString() const { return filterProtoS; }
    const proto::ProtoString* getMapProtoString() const { return mapProtoS; }
    const proto::ProtoString* getRangeProtoString() const { return rangeProtoS; }
    const proto::ProtoString* getBoolTypeString() const { return boolTypeS; }
    const proto::ProtoString* getFilterBoolString() const { return filterBoolS; }
    const proto::ProtoObject* getImportErrorType() const { return importErrorType; }
    const proto::ProtoObject* getAttributeErrorType() const { return attributeErrorType; }
    const proto::ProtoObject* getTuplePrototype() const { return tuplePrototype; }
    const proto::ProtoObject* getDictPrototype()  const { return dictPrototype; }
    const proto::ProtoObject* getNameErrorType() const { return nameErrorType; }
    const proto::ProtoObject* getZeroInteger() const { return zeroInteger; }
    const proto::ProtoObject* getOneInteger() const { return oneInteger; }
    const proto::ProtoObject* getRangeIteratorProto() const { return rangeIteratorProto; }
    void setRangeIteratorProto(const proto::ProtoObject* p) { rangeIteratorProto = p; }

    const proto::ProtoObject* getCodePrototype() const { return codePrototype; }

    const proto::ProtoString* getCodeString() const { return __code__; }
    const proto::ProtoString* getGlobalsString() const { return __globals__; }
    const proto::ProtoString* getCoVarnamesString() const { return co_varnames; }
    const proto::ProtoString* getCoNparamsString() const { return co_nparams; }
    const proto::ProtoString* getCoKwonlyargcountString() const { return co_kwonlyargcount; }
    const proto::ProtoString* getCoAutomaticCountString() const { return co_automatic_count; }
    const proto::ProtoString* getCoIsGeneratorString() const { return co_is_generator; }
    const proto::ProtoString* getCoFlagsString() const { return co_flags; }
    const proto::ProtoString* getCoConstsString() const { return co_consts; }
    const proto::ProtoString* getCoNameString() const { return co_name; }
    const proto::ProtoString* getCoNamesString() const { return co_names; }
    const proto::ProtoString* getCoCodeString() const { return co_code; }
    const proto::ProtoString* getCoNativeBytecodeString() const { return co_bytecode_native; }
    const proto::ProtoString* getFnMetaCacheString() const { return fn_meta_cache; }
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
    const proto::ProtoString* getDefaultsString() const { return __defaults__; }
    const proto::ProtoString* getKwdefaultsString() const { return __kwdefaults__; }

    const proto::ProtoString* getRangeString() const { return rangeS; }
    const proto::ProtoString* getItemsString() const { return itemsS; }
    const proto::ProtoString* getValuesString() const { return valuesS; }
    const proto::ProtoString* getKeysSString() const { return keysS; }
    const proto::ProtoString* getAssertionErrorSString() const { return assertionErrorS; }
    const proto::ProtoString* getRuntimeErrorSString() const { return runtimeErrorS; }
    const proto::ProtoString* getTypeErrorSString() const { return typeErrorS; }
    const proto::ProtoString* getKeyErrorSString() const { return keyErrorS; }
    const proto::ProtoString* getValueErrorSString() const { return valueErrorS; }
    const proto::ProtoString* getStopIterationSString() const { return stopIterationS; }
    const proto::ProtoString* getStopAsyncIterationSString() const { return stopAsyncIterationS; }
    const proto::ProtoString* getExceptionSString() const { return exceptionS; }
    const proto::ProtoString* getNameErrorSString() const { return nameErrorS; }
    const proto::ProtoString* getAttributeErrorSString() const { return attributeErrorS; }
    const proto::ProtoString* getSyntaxErrorSString() const { return syntaxErrorS; }
    const proto::ProtoString* getImportErrorSString() const { return importErrorS; }
    const proto::ProtoString* getIndexErrorSString() const { return indexErrorS; }

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
    const proto::ProtoString* getSlotsString() const { return slotsString; }
    const proto::ProtoString* getIsSuperProxyString() const { return isSuperProxyString; }
    const proto::ProtoString* getPyGetAttrHandlerString() const { return pyGetAttrHandlerString_; }
    const proto::ProtoString* getIsPythonClassString() const { return isPythonClassString; }

    /**
     * P2 type flags cache (May 2026).
     *
     * Per-class precomputed bitset answering the four most common protoCore
     * walks every LOAD_ATTR / STORE_ATTR fast path needs:
     *
     *   bit 0  IS_PYTHON_CLASS    obj is itself a class object
     *   bit 1  HAS_SLOTS          class declares __slots__
     *   bit 2  HAS_DATA_DESCR     any attr in MRO defines __set__ or __delete__
     *   bit 3  HAS_GET_DESCR      any attr in MRO defines __get__
     *   bit 31 COMPUTED           sentinel — distinguishes "all flags zero,
     *                             already computed" from "never computed"
     *
     * The flags are stored as a SmallInt own-attribute keyed by
     * `__pyflags__` on each class.  First read computes; subsequent reads
     * are a single getOwnAttributeDirect (cached) — same cost as today's
     * isPythonClass / slots / descriptor probes, but covering all four
     * answers in one shot.  See ensureClassFlags / fastClassFlags below.
     */
    static constexpr uint32_t PYFLAG_IS_CLASS              = 1u << 0;
    static constexpr uint32_t PYFLAG_HAS_SLOTS             = 1u << 1;
    static constexpr uint32_t PYFLAG_HAS_DATA_DESCR        = 1u << 2;
    static constexpr uint32_t PYFLAG_HAS_GET_DESCR         = 1u << 3;
    // Set when any MRO entry (other than the built-in object/type
    // prototypes that provide the default implementation) owns its own
    // __getattribute__ attribute.  Forces attribute reads through the
    // slow path so the user-defined hook actually intercepts every
    // access, including own-instance attribute lookups.
    static constexpr uint32_t PYFLAG_HAS_CUSTOM_GETATTR    = 1u << 4;
    static constexpr uint32_t PYFLAG_COMPUTED              = 1u << 31;

    /** Fast read of cached flags; returns 0 when not yet computed. */
    uint32_t fastClassFlags(proto::ProtoContext* ctx, const proto::ProtoObject* cls) const;

    /** Compute (if absent) and return cached flags for `cls`. */
    uint32_t ensureClassFlags(proto::ProtoContext* ctx, const proto::ProtoObject* cls);

    const proto::ProtoString* getGetattrDunderString() const { return getattrDunderString; }
    const proto::ProtoString* getGetattributeDunderString() const { return getattributeDunderString; }
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
    const proto::ProtoString* getGiBlocksString() const { return gi_blocks; }

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
    
    /** Sets the recursion limit (sys.setrecursionlimit). */
    void setRecursionLimit(int limit) { recursionLimit_ = limit; }
    /** Gets the recursion limit (sys.getrecursionlimit). */
    int getRecursionLimit() const { return recursionLimit_; }

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
     * @brief Pushes an exception to the active exception stack (used by except blocks).
     */
    static void pushActiveException(const proto::ProtoObject* exc);

    /**
     * @brief Pops the top exception from the active exception stack (used by OP_POP_EXCEPT).
     */
    static void popActiveException();

    /**
     * @brief Gets the currently active exception in the innermost except block.
     */
    static const proto::ProtoObject* getActiveException();

    /**
     * @brief Helper to save the thread's pending exception context and restore it on destruction.
     * Mirrors CPython's PyErr_Fetch / PyErr_Restore semantics to protect surrounding try-except
     * blocks from being overwritten by internal C++ exception handling.
     */
    class ExceptionStateSaver {
        PythonEnvironment* env;
        const proto::ProtoObject* savedExc;
    public:
        ExceptionStateSaver(PythonEnvironment* e) : env(e), savedExc(nullptr) {
            if (env) savedExc = env->peekPendingException();
        }
        ~ExceptionStateSaver() {
            if (env) {
                // Restore the saved pending exception (protects surrounding Python blocks).
                env->setPendingException(savedExc);
            }
        }
    };

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
    
    static thread_local PythonEnvironment* s_threadEnv;
    static thread_local proto::ProtoContext* s_threadContext;
    static thread_local int s_recursionDepth;
    static thread_local bool s_inRecursionError;
    static thread_local const proto::ProtoObject* s_currentFrame;
    static thread_local const proto::ProtoObject* s_currentGlobals;
    static thread_local const proto::ProtoObject* s_currentCodeObject;
    /**
     * @brief Fast-path mirror of the per-thread pending-exception slot.
     *
     * The authoritative storage is the `_pending_exc` attribute on the
     * per-thread Python thread object (see `setPendingException` /
     * `peekPendingException`).  Every call to `hasPendingException()` from
     * the bytecode dispatcher would otherwise pay a full `getAttribute`
     * resolution (cache check, mutable-snapshot resolution, attribute walk)
     * on every iteration that needs an exception check — 52 such call sites
     * inside `executeBytecodeRange`, called millions of times on
     * call_recursion (≈ 4 % of total CPU per profile).  This bool mirrors
     * the slot's set/clear state so the hot check collapses to a single TLS
     * bool read; `peekPendingException` / `takePendingException` still walk
     * the authoritative attribute path because they need the actual value.
     */
    static thread_local bool s_pendingExcFlag;

    /**
     * @brief TLS mirror of the pending-exception object.
     *
     * The authoritative storage remains the `_pending_exc` attribute on
     * the per-thread Python thread object — that is what keeps the
     * exception alive across GC cycles (the py_thread is rooted via
     * `s_globalThreadRootsDict`).  Reading the storage through
     * `getAttribute`, however, is mediated by protoCore's per-thread
     * attribute cache, which is keyed on the resolved snapshot pointer
     * for mutable objects.  Under sustained mutable-shard contention
     * (observed empirically at 4+ consecutive `json.dumps` calls in a
     * module body) the cache could occasionally serve a stale entry
     * that returned `nullptr` even though `setPendingException` had
     * just stored a valid exception object.  The dispatcher would then
     * see `hasPendingException()==true` (TLS bool) but
     * `peekPendingException()==nullptr` and bail out of the frame
     * silently — the SP0-P2.5 silent-halt bug.
     *
     * Mirroring the pointer in TLS makes the read path completely
     * cache-independent: `peekPendingException()` returns the TLS
     * pointer directly.  The TLS slot is updated in lockstep with the
     * authoritative storage in `setPendingException`,
     * `clearPendingException`, and `takePendingException`, so the two
     * stay consistent by construction.  Because the storage write
     * still happens on every set/clear, the GC reachability story is
     * unchanged.
     */
    static thread_local const proto::ProtoObject* s_pendingExc;

    /**
     * @brief TLS mirror of the active-exception stack (per-thread).
     *
     * Read path for `getActiveException`.  Same rationale as
     * `s_pendingExc`: the prior authoritative storage on `_active_excs`
     * (a ProtoList attribute on the per-thread py_thread object) went
     * through protoCore's mutable-shard attribute cache, which could
     * serve stale (null) results when a value was set in context A and
     * read back in context B even though both share the same OS thread.
     * That manifested as the `RuntimeError: reraise outside of except
     * block` SP-G/B5 bug.
     *
     * GC reachability is provided by `activeExcsRoots_` (Mechanism B,
     * `ProtoRootSet`): every push pins the exception there, every pop
     * releases the corresponding handle (parallel-stacked in
     * `s_activeExcsHandles`).  The plain `std::vector` of bare
     * `ProtoObject*` is safe precisely because the root set keeps
     * each entry reachable from the GC's mark phase — and a
     * `std::vector` (rather than a ProtoList) sidesteps the
     * cross-context safety footguns of cached protoCore collection
     * pointers (a ProtoList allocated in context A and reused from
     * context B trips dirty-segment assertions).
     */
    static thread_local std::vector<const proto::ProtoObject*> s_activeExcs;

    /**
     * @brief TLS stack of `ProtoRootSet::Handle` pins parallel to
     *        `s_activeExcs`.  See `activeExcsRoots_`.
     *
     * On `pushActiveException`: `h = activeExcsRoots_->add(exc);
     *                            s_activeExcsHandles.push_back(h);`.
     * On `popActiveException`:  `activeExcsRoots_->remove(s_activeExcsHandles.back());
     *                            s_activeExcsHandles.pop_back();`.
     *
     * The handle is a 64-bit integer with embedded generation, so a
     * stale `remove` is a silent no-op (see protoCore.h ProtoRootSet
     * docs).  The two stacks (`s_activeExcs`, `s_activeExcsHandles`)
     * grow and shrink in lockstep; their sizes must always match.
     */
    static thread_local std::vector<proto::ProtoRootSet::Handle> s_activeExcsHandles;

    /**
     * @brief Returns true if there is a pending exception.
     *
     * Inline fast path: a single TLS bool read.  Defined in the header so
     * the hot dispatcher inlines it instead of paying a cross-DSO call.
     */
    bool hasPendingException() const {
        return s_threadContext != nullptr && s_pendingExcFlag;
    }

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
    void raiseUnboundLocalError(proto::ProtoContext* context, const std::string& msg);
    void raiseAttributeError(proto::ProtoContext* context, const proto::ProtoObject* obj, const std::string& attr);
    /**
     * @brief Records an AttributeError where the human-readable message and the
     *        machine-readable `name` slot must differ.  Used by the property
     *        descriptor protocol where CPython emits messages like
     *        `property 'x' of 'C' object has no setter` rather than the generic
     *        `'C' object has no attribute 'x'` shape.  The `attr` argument is
     *        stored verbatim in the AttributeError's `name` slot so callers
     *        catching AttributeError can still introspect the offending name.
     */
    void raiseAttributeErrorWithMessage(proto::ProtoContext* context, const proto::ProtoObject* obj, const std::string& message, const std::string& attr);
    void raiseRuntimeError(proto::ProtoContext* context, const std::string& msg);
    void raiseOSError(proto::ProtoContext* context, int errnum, const std::string& strerr, const std::string& filename = "");
    void raiseTypeError(proto::ProtoContext* context, const std::string& msg);
    void raiseImportError(proto::ProtoContext* context, const std::string& msg);
    void raiseKeyboardInterrupt(proto::ProtoContext* context);
    void raiseSyntaxError(proto::ProtoContext* context, const std::string& msg, int lineno, int offset, const std::string& text);
    /**
     * Emit a SyntaxWarning via _py_warnings.warn_explicit.  Honours the
     * active warnings filter chain (catch_warnings(record=True), simplefilter,
     * etc.).  When the filter is 'error', the warning is converted to a
     * SyntaxError with the same message and lineno per CPython's compile-time
     * semantics.  Returns true if a SyntaxError was raised (caller should
     * stop further compilation), false otherwise.
     */
    bool emitSyntaxWarning(proto::ProtoContext* context, const std::string& msg, const std::string& filename, int lineno);
    void raiseSystemExit(proto::ProtoContext* context, int code);
    void raiseEOFError(proto::ProtoContext* ctx);
    void raiseRecursionError(proto::ProtoContext* context);
    void raiseAssertionError(proto::ProtoContext* ctx, const proto::ProtoObject* msg = nullptr);
    void raiseZeroDivisionError(proto::ProtoContext* ctx);
    void raiseIndexError(proto::ProtoContext* context, const std::string& msg);
    void raiseStopIteration(proto::ProtoContext* context, const proto::ProtoObject* value = nullptr);
    void raiseStopAsyncIteration(proto::ProtoContext* context);
    
    /**
     * @brief Returns the string representation of an object (Step 1120).
     */
    static std::string reprObject(proto::ProtoContext* context, const proto::ProtoObject* obj);

    /**
     * @brief Returns true if the object is a StopIteration exception.
     */
    bool isStopIteration(proto::ProtoContext* ctx, const proto::ProtoObject* exc) const;

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
    std::string formatTraceback(const proto::ProtoContext* ctx, const proto::ProtoObject* exc = nullptr);
    
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

    /** Helper to register a native module with generic decoration (__name__, __class__) */
    void registerNativeModule(NativeModuleProvider* provider, const std::string& name, std::function<const proto::ProtoObject*(proto::ProtoContext*)> init);

    proto::ProtoSpace* space_;
    proto::ProtoContext* rootContext_;

    /**
     * @brief GC root set that pins active exceptions for their
     *        bounded lifetime (push/pop in `pushActiveException`
     *        / `popActiveException`).
     *
     * Replaces the prior `_active_excs` ProtoList attribute on
     * py_thread.  That attribute path round-tripped through the
     * mutable-shard attribute cache — the same cache whose stale-read
     * behaviour motivated the TLS read mirror in the first place
     * (SP-G/B5).  Pinning each pushed exception in a `ProtoRootSet`
     * keeps the values reachable from the tracing GC without the
     * cache hazard, and lets the read path stay on the cache-free
     * `s_activeExcs` TLS vector.
     *
     * Owned by this `PythonEnvironment`: created in the constructor,
     * destroyed in the destructor (`space_->destroyRootSet`).
     */
    proto::ProtoRootSet* activeExcsRoots_{nullptr};

    /**
     * @brief Per-environment root set for transient pins of native-call
     *        arguments.
     *
     * When the bytecode dispatcher invokes a native C method (e.g.
     * py_str_join), the args ProtoList is built from the operand stack
     * and the stack slots are popped before the call. The args list
     * therefore lives only as a C++ local for the duration of the
     * native call — invisible to the GC. If the native method calls
     * back into Python (e.g. env->next on a generator) and that
     * callback allocates enough cells to trigger GC, the args list and
     * everything reachable through it (the iterable, the iterator's
     * own backing cells) become unreachable and are freed under the
     * running mutator.
     *
     * `invokeCallable` pins the args list in this root set across each
     * native call via the `TransientArgsPin` RAII guard below; the
     * GC traces every entry as a root, keeping the iterable alive.
     */
    proto::ProtoRootSet* transientArgsRoots_{nullptr};

public:
    proto::ProtoRootSet* getTransientArgsRoots() const { return transientArgsRoots_; }

    /**
     * @brief RAII pin for a `ProtoObject*` held in a C++ local across a
     *        callback that may trigger GC.
     *
     * Construct: takes the env and the cell handle to pin.
     * Lifetime:  the pointer is registered as a GC root via
     *            `transientArgsRoots_`.
     * Destruct:  the pointer is unpinned automatically (panic-safe).
     *
     * Usage:
     * ```cpp
     * const proto::ProtoObject* it = env->iter(iterable);
     * PythonEnvironment::TransientPin pinIt(env, it);
     * for (;;) {
     *     auto item = env->next(it);   // GC-safe across this call
     *     if (!item) break;
     *     // ...use item...
     * }
     * // pinIt destructor releases the pin here
     * ```
     *
     * Non-copyable, non-movable so that the pin is bound to a single
     * stack lifetime. Pinning a null pointer is a silent no-op (kept
     * cheap for code paths that may receive nullptr without branching).
     */
    class TransientPin {
    public:
        TransientPin(PythonEnvironment* env, const proto::ProtoObject* obj)
            : roots_(env ? env->getTransientArgsRoots() : nullptr),
              h_(roots_ && obj ? roots_->add(obj) : proto::ProtoRootSet::kNullHandle) {}
        ~TransientPin() {
            if (roots_ && h_ != proto::ProtoRootSet::kNullHandle) roots_->remove(h_);
        }
        TransientPin(const TransientPin&) = delete;
        TransientPin& operator=(const TransientPin&) = delete;
        TransientPin(TransientPin&&) = delete;
        TransientPin& operator=(TransientPin&&) = delete;
    private:
        proto::ProtoRootSet* roots_;
        proto::ProtoRootSet::Handle h_;
    };

private:

    const proto::ProtoObject* rangeIteratorProto{nullptr};
    const proto::ProtoObject* genericAliasProto{nullptr};
    const proto::ProtoObject* unionTypeProto{nullptr};

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
    const proto::ProtoObject* ellipsisPrototype;
    const proto::ProtoObject* notImplementedPrototype;
    const proto::ProtoObject* noneTypeProto;
    const proto::ProtoObject* framePrototype;
    const proto::ProtoObject* generatorPrototype;
    const proto::ProtoObject* coroutinePrototype = nullptr;
    const proto::ProtoObject* asyncGeneratorPrototype = nullptr;
    const proto::ProtoObject* functionPrototype;
    const proto::ProtoObject* sliceType;
    const proto::ProtoObject* frozensetPrototype;
    const proto::ProtoObject* floatPrototype;
    const proto::ProtoObject* complexPrototype;
    const proto::ProtoObject* boolPrototype;
    const proto::ProtoObject* modulePrototype;
    const proto::ProtoObject* mappingProxyPrototype;
    const proto::ProtoObject* methodPrototype;
    const proto::ProtoObject* unionTypePrototype;
    const proto::ProtoObject* tracebackPrototype;
    const proto::ProtoObject* cellPrototype;
    const proto::ProtoObject* codePrototype;
    const proto::ProtoObject* getSetDescriptorPrototype;
    const proto::ProtoObject* methodDescriptorPrototype{nullptr};
    const proto::ProtoObject* wrapperDescriptorPrototype{nullptr};
    const proto::ProtoObject* memberDescriptorPrototype{nullptr};
    const proto::ProtoObject* builtinFunctionOrMethodPrototype{nullptr};
    const proto::ProtoObject* superPrototype{nullptr};

public:
    const proto::ProtoObject* getMethodDescriptorPrototype() const { return methodDescriptorPrototype; }
    const proto::ProtoObject* getWrapperDescriptorPrototype() const { return wrapperDescriptorPrototype; }
    const proto::ProtoObject* getMemberDescriptorPrototype() const { return memberDescriptorPrototype; }
    const proto::ProtoObject* getBuiltinFunctionOrMethodPrototype() const { return builtinFunctionOrMethodPrototype; }
    const proto::ProtoObject* getUnionTypePrototype() const { return unionTypePrototype; }
    const proto::ProtoObject* getStopAsyncIterationType() const { return stopAsyncIterationType; }
    /** Process-singleton "unbound local" sentinel.  Compiler stores this
        into CO_OPTIMIZED slots that correspond to annotation-only locals
        (`x: int` with no initial value).  LOAD_FAST detects it and raises
        UnboundLocalError, matching CPython's PEP 526 semantics. */
    const proto::ProtoObject* getUnboundSentinel() const { return unboundSentinel_; }
private:
    const proto::ProtoObject* sysModule;
    const proto::ProtoObject* builtinsModule;
    const proto::ProtoObject* exceptionsModule;
    std::string stdLibPath_;
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
    const proto::ProtoObject* runtimeErrorType{nullptr};
    const proto::ProtoObject* importErrorType{nullptr};
    const proto::ProtoObject* moduleNotFoundErrorType{nullptr};
    const proto::ProtoObject* baseExceptionType{nullptr};
    const proto::ProtoObject* keyboardInterruptType{nullptr};
    const proto::ProtoObject* systemExitType{nullptr};
    const proto::ProtoObject* recursionErrorType{nullptr};
    const proto::ProtoObject* stopIterationType = nullptr;
    const proto::ProtoObject* eofErrorType = nullptr;
    const proto::ProtoObject* assertionErrorType = nullptr;
    const proto::ProtoObject* generatorExitType = nullptr;
    const proto::ProtoObject* arithmeticErrorType = nullptr;
    const proto::ProtoObject* lookupErrorType = nullptr;
    const proto::ProtoObject* unboundLocalErrorType = nullptr;
    const proto::ProtoObject* zeroDivisionErrorType = nullptr;
    const proto::ProtoObject* indexErrorType{nullptr};
    const proto::ProtoObject* systemErrorType{nullptr};
    const proto::ProtoObject* stopAsyncIterationType{nullptr};
    const proto::ProtoObject* unboundSentinel_{nullptr};
    const proto::ProtoObject* osErrorType{nullptr};
    const proto::ProtoObject* blockingIOErrorType{nullptr};
    const proto::ProtoObject* warningType{nullptr};
    const proto::ProtoObject* userWarningType{nullptr};
    const proto::ProtoObject* deprecationWarningType{nullptr};
    const proto::ProtoObject* runtimeWarningType{nullptr};
    const proto::ProtoObject* pendingDeprecationWarningType{nullptr};
    const proto::ProtoObject* importWarningType{nullptr};
    const proto::ProtoObject* bytesWarningType{nullptr};
    const proto::ProtoObject* resourceWarningType{nullptr};
    const proto::ProtoObject* encodingWarningType{nullptr};
    const proto::ProtoList* taskQueue{nullptr};
    const proto::ProtoString* iterString{nullptr};
    const proto::ProtoString* nextString{nullptr};
    const proto::ProtoList* emptyList{nullptr};
    const proto::ProtoSparseList* emptySparseList{nullptr};
    int recursionLimit_{1000};

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

    const proto::ProtoString* rangeS{nullptr};
    const proto::ProtoString* itemsS{nullptr};
    const proto::ProtoString* valuesS{nullptr};
    const proto::ProtoString* keysS{nullptr};
    const proto::ProtoString* assertionErrorS{nullptr};
    const proto::ProtoString* runtimeErrorS{nullptr};
    const proto::ProtoString* typeErrorS{nullptr};
    const proto::ProtoString* keyErrorS{nullptr};
    const proto::ProtoString* valueErrorS{nullptr};
    const proto::ProtoString* stopIterationS{nullptr};
    const proto::ProtoString* stopAsyncIterationS{nullptr};
    const proto::ProtoString* exceptionS{nullptr};
    const proto::ProtoString* nameErrorS{nullptr};
    const proto::ProtoString* attributeErrorS{nullptr};
    const proto::ProtoString* syntaxErrorS{nullptr};
    const proto::ProtoString* importErrorS{nullptr};
    const proto::ProtoString* indexErrorS{nullptr};
    const proto::ProtoString* osErrorS{nullptr};
    const proto::ProtoString* blockingIOErrorS{nullptr};

    const proto::ProtoString* classString{nullptr};
    const proto::ProtoString* nameString{nullptr};
    const proto::ProtoString* callString{nullptr};
    const proto::ProtoString* newString{nullptr};
    const proto::ProtoString* prepareString{nullptr};
    const proto::ProtoString* getItemString{nullptr};
    const proto::ProtoString* pathS{nullptr};
    const proto::ProtoString* modulesS{nullptr};

    const proto::ProtoString* pendingExcString{nullptr};
    const proto::ProtoString* activeExcsString{nullptr};

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
    const proto::ProtoString* addS{nullptr};
    const proto::ProtoString* reversedString{nullptr};
    
    // Keyword Names Stack (thread-local per Environment)
    const proto::ProtoList* kwNamesStack = nullptr;
    const proto::ProtoString* getDunderString{nullptr};
    const proto::ProtoString* setDunderString{nullptr};
    const proto::ProtoString* delDunderString{nullptr};
    const proto::ProtoString* __closure__{nullptr};
    const proto::ProtoString* __defaults__{nullptr};
    const proto::ProtoString* __kwdefaults__{nullptr};
    
    const proto::ProtoString* mroString{nullptr};
    const proto::ProtoString* basesString{nullptr};
    // Cached symbol for the per-class marker that lets isActuallyAClass
    // collapse to a single hasOwnAttribute call on the hot path
    // (Phase 4 of the protoCore-delegation design).
    const proto::ProtoString* isPythonClassString{nullptr};
    const proto::ProtoString* pyFlagsString_{nullptr};  // P2: type flags cache key

    const proto::ProtoString* enumProtoS{nullptr};
    const proto::ProtoString* revProtoS{nullptr};
    const proto::ProtoString* zipProtoS = nullptr;
    
    const proto::ProtoString* coFilenameString = nullptr;
    const proto::ProtoString* coFirstLinenoString = nullptr;
    const proto::ProtoString* coLnotabString = nullptr;
    const proto::ProtoString* filterProtoS{nullptr};
    const proto::ProtoString* mapProtoS{nullptr};
    const proto::ProtoString* rangeProtoS{nullptr};
    const proto::ProtoString* boolTypeS{nullptr};
    const proto::ProtoString* filterBoolS{nullptr};

    const proto::ProtoString* __code__{nullptr};
    const proto::ProtoString* __globals__{nullptr};
    const proto::ProtoString* co_varnames{nullptr};
    const proto::ProtoString* co_nparams{nullptr};
    const proto::ProtoString* co_kwonlyargcount{nullptr};
    const proto::ProtoString* co_automatic_count{nullptr};
    const proto::ProtoString* co_is_generator{nullptr};
    const proto::ProtoString* co_flags{nullptr};
    const proto::ProtoString* co_consts{nullptr};
    const proto::ProtoString* co_name{nullptr};
    const proto::ProtoString* co_names{nullptr};
    const proto::ProtoString* co_code{nullptr};
    const proto::ProtoString* co_positions{nullptr};
    const proto::ProtoString* co_bytecode_native{nullptr};

    const proto::ProtoString* fn_meta_cache{nullptr};
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
    const proto::ProtoString* gi_blocks{nullptr};
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
    const proto::ProtoString* slotsString{nullptr};
    const proto::ProtoString* isSuperProxyString{nullptr};
    const proto::ProtoString* pyGetAttrHandlerString_{nullptr};
    const proto::ProtoString* getattrDunderString{nullptr};
    const proto::ProtoString* getattributeDunderString{nullptr};
    const proto::ProtoString* matMulString{nullptr};
    const proto::ProtoString* imatmulString{nullptr};
    const proto::ProtoString* rmatmulString{nullptr};
    const proto::ProtoString* initString{nullptr};
    const proto::ProtoString* executedString{nullptr};

    const proto::ProtoString* startString{nullptr};
    const proto::ProtoString* stopString{nullptr};
    const proto::ProtoString* stepString{nullptr};
    const proto::ProtoString* moduleString{nullptr};
    const proto::ProtoString* builtinsString{nullptr};

    const proto::ProtoString* ioModuleString{nullptr};
    const proto::ProtoString* openString{nullptr};
    const proto::ProtoString* exceptionRootS{nullptr};

    const proto::ProtoString* getExceptionRootString() const { return exceptionRootS; }
    const proto::ProtoString* getAddS() const { return addS; }

    void clearExceptionsWrapper() { clearPendingException(); }
    void setPendingExceptionWrapper(const proto::ProtoObject* exc) { setPendingException(exc); };

    const proto::ProtoObject* zeroInteger{nullptr};
    const proto::ProtoObject* oneInteger{nullptr};

    const proto::ProtoString* fileDunderS{nullptr};
    const proto::ProtoString* pathDunderS{nullptr};
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

    std::unordered_map<std::string, const proto::ProtoString*> internPool_;
    std::mutex internMutex_;

    // STRUCT-1: native-method introspection side table.  Maps a native
    // ProtoMethod function pointer to the attribute name it was
    // registered under and the prototype that owns it.  Populated by a
    // one-time post-bootstrap walk (registerNativeMethodNames()).  Lets
    // env->getAttribute synthesise `__name__` / `__qualname__` /
    // `__objclass__` for POINTER_TAG_METHOD bound methods (e.g.
    // `[].__add__.__name__ == '__add__'`) without changing the
    // tagged-pointer binding form — keeping all 300+ asMethod() call
    // sites intact.
public:
    struct NativeMethodInfo {
        std::string name;
        const proto::ProtoObject* owningClass;
        // Descriptor taxonomy: a native method bound on a class prototype
        // is either a method_descriptor (regular method — str.lower) or a
        // wrapper_descriptor (type-slot dunder — int.__add__).  Drives
        // getType() / repr() so they match CPython.
        enum class Kind { METHOD, WRAPPER };
        Kind kind{Kind::METHOD};
    };
private:
    std::unordered_map<const void*, NativeMethodInfo> nativeMethodNames_;
public:
    /** Record a native method fn-pointer -> (name, owning class, kind). */
    void recordNativeMethodName(const void* fnPtr, const std::string& name,
                                 const proto::ProtoObject* owningClass,
                                 NativeMethodInfo::Kind kind = NativeMethodInfo::Kind::METHOD);
    /** Look up the registered name for a native ProtoMethod fn-pointer.
     *  Returns false when unknown.  outKind is optional. */
    bool lookupNativeMethodInfo(const void* fnPtr, std::string& outName,
                                 const proto::ProtoObject** outOwningClass,
                                 NativeMethodInfo::Kind* outKind = nullptr) const;
    /** One-time post-bootstrap walk: scan every built-in prototype's
     *  own attributes and register each native-method value. */
    void registerNativeMethodNames(proto::ProtoContext* ctx);
private:

    /** Incremented on invalidateResolveCache(); per-thread caches check this (lock-free). */
    mutable std::atomic<uint64_t> resolveCacheGeneration_{0};
    std::istream* stdin_{&std::cin};

public:
    void incrementResolveCacheGeneration() { resolveCacheGeneration_.fetch_add(1, std::memory_order_release); }
    uint64_t resolveCacheGeneration() const { return resolveCacheGeneration_.load(std::memory_order_acquire); }

    // Sprint-4 hook for OP_LOAD_ATTR fast path: query the per-thread
    // getType cache for this obj's primitive classification.
    //   0 -> not cached (caller must fall back to isString/isInteger/...)
    //   1 -> cached AND obj is a primitive (str/int/bool/float or subclass)
    //   2 -> cached AND obj is NOT a primitive
    // The bottom-path of getType() only ever populates with state 2, so
    // attribute-heavy workloads on instances see "is this a primitive"
    // resolved as 2 after the first access — 4 isXxx tag checks (~20 ns)
    // collapse to one cache lookup + a single branch.
    int primitiveCacheHit(const proto::ProtoObject* obj) const;

    /** Signal handling flag (Step 1310). */
    static std::atomic<bool> s_sigintReceived;
    static std::thread::id s_mainThreadId;

    /** RAII scope for managing thread-local Python environment and context registration. */
    class ContextScope {
    public:
        ContextScope(PythonEnvironment* env, proto::ProtoContext* ctx) : ctx_(ctx) {
            prevEnv_ = PythonEnvironment::s_threadEnv;
            prevCtx_ = PythonEnvironment::s_threadContext;
            PythonEnvironment::registerContext(ctx, env);
        }
        ~ContextScope() {
            if (protoPython::diagThreadEnabled()) std::cerr << "[proto-thread] ContextScope destruction ctx=" << ctx_ << " tid=" << std::this_thread::get_id() << "\n" << std::flush;
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

    /** StopIteration helpers */
    bool handleExhaustion(proto::ProtoContext* ctx);
};

} // namespace protoPython

#endif // PROTOPYTHON_PYTHONENVIRONMENT_H
