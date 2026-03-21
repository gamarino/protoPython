#ifndef PROTOPYTHON_IMPMODULE_H
#define PROTOPYTHON_IMPMODULE_H

#include <protoPython/PythonEnvironment.h>

namespace protoPython {
namespace library {

class ImpModule {
public:
    static const proto::ProtoObject* createImpModule(proto::ProtoContext* ctx);
};

} // namespace library
} // namespace protoPython

#endif // PROTOPYTHON_IMPMODULE_H
