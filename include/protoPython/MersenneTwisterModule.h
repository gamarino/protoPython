#ifndef PROTOPYTHON_MERSENNE_TWISTER_MODULE_H
#define PROTOPYTHON_MERSENNE_TWISTER_MODULE_H

#include <protoCore.h>

namespace protoPython {
namespace mersenne_twister {
    /** The native core of _random.Random (module `_mt19937`). */
    const proto::ProtoObject* initialize(proto::ProtoContext* ctx);
}
}

#endif
