#include <protoPython/StructSequence.h>
#include <protoPython/PythonEnvironment.h>

namespace protoPython {

const proto::ProtoObject* newStructSequence(
    proto::ProtoContext* ctx,
    const std::vector<StructSequenceField>& fields,
    size_t nVisible) {
    if (nVisible > fields.size()) nVisible = fields.size();

    // Every cell built here is a young cell of ctx, so it stays reachable for
    // the collector until ctx returns; no extra pinning is needed.
    const proto::ProtoList* visible = ctx->newList();
    for (size_t i = 0; i < nVisible; ++i) {
        visible = visible->appendLast(ctx, fields[i].value ? fields[i].value : PROTO_NONE);
    }
    const proto::ProtoTuple* data = ctx->newTupleFromList(visible);

    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* obj = ctx->newObject(true);
    if (env && env->getTuplePrototype()) {
        obj = obj->addParent(ctx, env->getTuplePrototype());
        obj = obj->setAttribute(ctx, env->getClassString(), env->getTuplePrototype());
    }
    obj = obj->setAttribute(ctx,
        env ? env->getDataString() : PythonEnvironment::getInternedString(ctx, "__data__"),
        data ? data->asObject(ctx) : visible->asObject(ctx));
    for (const StructSequenceField& field : fields) {
        if (!field.name) continue;
        obj = obj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, field.name),
            field.value ? field.value : PROTO_NONE);
    }
    return obj;
}

} // namespace protoPython
