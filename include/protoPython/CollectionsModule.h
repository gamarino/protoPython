#ifndef PROTOPYTHON_COLLECTIONSMODULE_H
#define PROTOPYTHON_COLLECTIONSMODULE_H

#include <protoCore.h>

namespace protoPython {
class PythonEnvironment;
namespace collections {

const proto::ProtoObject* initialize(proto::ProtoContext* ctx, PythonEnvironment* env);

} // namespace collections
} // namespace protoPython

#endif // PROTOPYTHON_COLLECTIONSMODULE_H
