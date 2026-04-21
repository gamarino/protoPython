#ifndef PROTOPYTHON_POSIXSUBPROCESS_MODULE_H
#define PROTOPYTHON_POSIXSUBPROCESS_MODULE_H

#include <protoCore.h>

namespace protoPython {
class PythonEnvironment;

namespace posixsubprocess_module {
const proto::ProtoObject* initialize(proto::ProtoContext* ctx, PythonEnvironment* env);
} // namespace posixsubprocess_module

} // namespace protoPython

#endif
