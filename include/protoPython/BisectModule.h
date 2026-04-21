#ifndef PROTOPYTHON_BISECT_MODULE_H
#define PROTOPYTHON_BISECT_MODULE_H

#include <protoCore.h>

namespace protoPython {
class PythonEnvironment;

namespace bisect {
    const proto::ProtoObject* initialize(proto::ProtoContext* ctx);
}
}

#endif
