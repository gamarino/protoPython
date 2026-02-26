#ifndef PROTO_PYTHON_CONTEXTVARS_MODULE_H
#define PROTO_PYTHON_CONTEXTVARS_MODULE_H

#include <protoCore.h>

namespace protoPython {
namespace contextvars {

const proto::ProtoObject* initialize(proto::ProtoContext* ctx);

} // namespace contextvars
} // namespace protoPython

#endif // PROTO_PYTHON_CONTEXTVARS_MODULE_H
