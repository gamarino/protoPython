#ifndef PROTOPYTHON_COLLECTIONSMODULE_H
#define PROTOPYTHON_COLLECTIONSMODULE_H

#include <protoCore.h>

namespace protoPython {
namespace collections {

const proto::ProtoObject* initialize(proto::ProtoContext* ctx, const proto::ProtoObject* dictPrototype);

} // namespace collections
} // namespace protoPython

#endif // PROTOPYTHON_COLLECTIONSMODULE_H
