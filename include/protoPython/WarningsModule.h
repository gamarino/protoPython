#pragma once

#include <protoCore.h>

namespace protoPython {
namespace library {

class WarningsModule {
public:
    /**
     * @brief Creates the built-in `_warnings` module instance.
     * @param context The proto context.
     * @return The `_warnings` module object.
     */
    static const proto::ProtoObject* createWarningsModule(proto::ProtoContext* context);
};

} // namespace library
} // namespace protoPython
