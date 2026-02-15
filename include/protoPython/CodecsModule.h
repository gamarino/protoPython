#ifndef PROTOPYTHON_CODECSMODULE_H
#define PROTOPYTHON_CODECSMODULE_H

#include <protoCore.h>

namespace protoPython {
namespace codecs {

const proto::ProtoObject* initialize(proto::ProtoContext* ctx,
                                     const proto::ProtoObject* objectProto,
                                     const proto::ProtoObject* typeProto);

} // namespace codecs
} // namespace protoPython

#endif // PROTOPYTHON_CODECSMODULE_H
