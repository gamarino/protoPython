#ifndef PROTOPYTHON_ERRNOMODULE_H
#define PROTOPYTHON_ERRNOMODULE_H

#include <protoCore.h>

namespace protoPython {
namespace errno_module {

const proto::ProtoObject* initialize(proto::ProtoContext* ctx);

} // namespace errno_module
} // namespace protoPython

#endif // PROTOPYTHON_ERRNOMODULE_H
