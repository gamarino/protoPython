#ifndef PROTOPYTHON_ATEXITMODULE_H
#define PROTOPYTHON_ATEXITMODULE_H

#include <protoCore.h>

namespace protoPython {
namespace atexit_module {

const proto::ProtoObject* initialize(proto::ProtoContext* ctx);

} // namespace atexit_module
} // namespace protoPython

#endif
