#ifndef PROTOPYTHON_FAULTHANDLER_MODULE_H
#define PROTOPYTHON_FAULTHANDLER_MODULE_H

#include <protoCore.h>

namespace protoPython {
namespace faulthandler {
    const proto::ProtoObject* initialize(proto::ProtoContext* ctx);
}
}

#endif
