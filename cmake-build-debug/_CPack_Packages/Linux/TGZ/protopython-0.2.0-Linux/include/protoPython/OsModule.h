#ifndef PROTOPYTHON_OSMODULE_H
#define PROTOPYTHON_OSMODULE_H

#include <protoCore.h>

namespace protoPython {
namespace os_module {

/** Initialize the _os module (getenv, getcwd, chdir). */
const proto::ProtoObject* initialize(proto::ProtoContext* ctx);

} // namespace os_module
} // namespace protoPython

#endif
