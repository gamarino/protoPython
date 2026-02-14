#ifndef PROTOPYTHON_EXCEPTIONSMODULE_H
#define PROTOPYTHON_EXCEPTIONSMODULE_H

#include <protoCore.h>

namespace protoPython {
namespace exceptions {

/**
 * @brief Initializes the exceptions module with Exception, KeyError, ValueError.
 */
const proto::ProtoObject* initialize(proto::ProtoContext* ctx,
                                     const proto::ProtoObject* objectProto,
                                     const proto::ProtoObject* typeProto);

} // namespace exceptions
} // namespace protoPython

#endif
