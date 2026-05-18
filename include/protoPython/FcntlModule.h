#ifndef PROTOPYTHON_FCNTL_MODULE_H
#define PROTOPYTHON_FCNTL_MODULE_H

#include <protoCore.h>

namespace protoPython {
class PythonEnvironment;

namespace fcntl_module {
const proto::ProtoObject* initialize(proto::ProtoContext* ctx, PythonEnvironment* env);
} // namespace fcntl_module

} // namespace protoPython

#endif
