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
     */
    PythonEnvironment();
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

private:
    void initializeRootObjects();

    proto::ProtoSpace space;
    proto::ProtoContext* context;

    const proto::ProtoObject* objectPrototype;
    const proto::ProtoObject* typePrototype;
    const proto::ProtoObject* intPrototype;
    const proto::ProtoObject* strPrototype;
    const proto::ProtoObject* listPrototype;
    const proto::ProtoObject* dictPrototype;
};

} // namespace protoPython

#endif // PROTOPYTHON_PYTHONENVIRONMENT_H
