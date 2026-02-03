#ifndef PROTOPYTHON_PYTHONENVIRONMENT_H
#define PROTOPYTHON_PYTHONENVIRONMENT_H

#include <protoCore.h>
#include <string>
#include <vector>

namespace protoPython {

class PythonEnvironment {
public:
    /**
     * @brief Initializes a new Python environment.
     * @param stdLibPath Path to the Python standard library. If empty, tries defaults.
     */
    PythonEnvironment(const std::string& stdLibPath = "", const std::vector<std::string>& searchPaths = {});
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
     * @brief Utility to resolve symbols in this environment.
     */
    const proto::ProtoObject* resolve(const std::string& name);

    /**
     * @brief Resolves the module by name, then invokes its \c main attribute if it is callable.
     * @param moduleName Module name (as used by resolve).
     * @return 0 on success (including when module has no \c main or it is not callable); non-zero on resolve failure.
     */
    int runModuleMain(const std::string& moduleName);

    /**
     * @brief Sets the global trace function (sys.settrace).
     */
    void setTraceFunction(const proto::ProtoObject* func) { traceFunction = func; }

    /**
     * @brief Gets the global trace function (sys.gettrace).
     */
    const proto::ProtoObject* getTraceFunction() const { return traceFunction; }

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
    const proto::ProtoObject* sysModule;
    const proto::ProtoObject* builtinsModule;
    const proto::ProtoObject* traceFunction{nullptr};
};

} // namespace protoPython

#endif // PROTOPYTHON_PYTHONENVIRONMENT_H
