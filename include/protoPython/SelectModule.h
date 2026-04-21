#ifndef PROTOPYTHON_SELECT_MODULE_H
#define PROTOPYTHON_SELECT_MODULE_H

#include <protoCore.h>

namespace protoPython {
class PythonEnvironment;

namespace select_module {
const proto::ProtoObject* initialize(proto::ProtoContext* ctx, PythonEnvironment* env);
} // namespace select_module

} // namespace protoPython

#endif
