#pragma once

#include <protoCore.h>

namespace protoPython {
namespace library {

class MarshalModule {
public:
    /**
     * @brief Creates the built-in `marshal` module instance.
     * @param context The proto context.
     * @return The `marshal` module object.
     */
    static const proto::ProtoObject* createMarshalModule(proto::ProtoContext* context);
};

} // namespace library
} // namespace protoPython
