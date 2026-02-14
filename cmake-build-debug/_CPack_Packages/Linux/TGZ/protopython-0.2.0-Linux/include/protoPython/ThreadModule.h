#ifndef PROTOPYTHON_THREADMODULE_H
#define PROTOPYTHON_THREADMODULE_H

#include <protoCore.h>

namespace protoPython {
namespace thread_module {

/** Initialize the _thread module (start_new_thread, join_thread). */
const proto::ProtoObject* initialize(proto::ProtoContext* ctx);

} // namespace thread_module
} // namespace protoPython

#endif
