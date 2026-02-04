#ifndef PROTOPYTHON_BUILTINSMODULE_H
#define PROTOPYTHON_BUILTINSMODULE_H

#include <protoCore.h>

namespace protoPython {
namespace builtins {

/**
 * @brief Initializes the builtins module.
 * @param ctx The context.
 * @param prototypes A struct or object containing the pre-initialized prototypes.
 */
const proto::ProtoObject* initialize(proto::ProtoContext* ctx, const proto::ProtoObject* objectProto,
                                   const proto::ProtoObject* typeProto, const proto::ProtoObject* intProto,
                                   const proto::ProtoObject* strProto, const proto::ProtoObject* listProto,
                                   const proto::ProtoObject* dictProto, const proto::ProtoObject* tupleProto,
                                   const proto::ProtoObject* setProto, const proto::ProtoObject* bytesProto,
                                   const proto::ProtoObject* sliceType, const proto::ProtoObject* frozensetProto,
                                   const proto::ProtoObject* floatProto = nullptr,
                                   const proto::ProtoObject* boolProto = nullptr,
                                   const proto::ProtoObject* ioModule = nullptr);

} // namespace builtins
} // namespace protoPython

#endif // PROTOPYTHON_BUILTINSMODULE_H
