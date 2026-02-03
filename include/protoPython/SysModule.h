#ifndef PROTOPYTHON_SYSMODULE_H
#define PROTOPYTHON_SYSMODULE_H

#include <protoCore.h>

namespace protoPython {
class PythonEnvironment;

namespace sys {

const proto::ProtoObject* initialize(proto::ProtoContext* ctx, PythonEnvironment* env);

} // namespace sys
} // namespace protoPython

#endif // PROTOPYTHON_SYSMODULE_H
