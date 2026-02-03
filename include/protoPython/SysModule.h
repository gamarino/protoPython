#ifndef PROTOPYTHON_SYSMODULE_H
#define PROTOPYTHON_SYSMODULE_H

#include <protoCore.h>
#include <vector>
#include <string>

namespace protoPython {
class PythonEnvironment;

namespace sys {

const proto::ProtoObject* initialize(proto::ProtoContext* ctx, PythonEnvironment* env,
                                     const std::vector<std::string>* argv = nullptr);

} // namespace sys
} // namespace protoPython

#endif // PROTOPYTHON_SYSMODULE_H
