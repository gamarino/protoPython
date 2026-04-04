#include "protoPython/StructModule.h"

namespace protoPython {
namespace struct_module {

const proto::ProtoObject* py_calcsize(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* link, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    return ctx->fromInteger(1);
}

const proto::ProtoObject* py_pack(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* link, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    return ctx->fromUTF8String(""); 
}

const proto::ProtoObject* py_unpack(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* link, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    return ctx->newList()->asObject(ctx);
}

const proto::ProtoObject* py_struct_init(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* link, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    return PROTO_NONE;
}

const proto::ProtoObject* py_struct_pack(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* link, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    return ctx->fromUTF8String(""); 
}

const proto::ProtoObject* py_struct_unpack(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* link, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    return ctx->newList()->asObject(ctx);
}

const proto::ProtoObject* py_clearcache(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* link, const proto::ProtoList* args, const proto::ProtoSparseList* kwargs) {
    return PROTO_NONE;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(false);

    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "calcsize"), ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_calcsize));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "pack"), ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_pack));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "unpack"), ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_unpack));
    
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "iter_unpack"), ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_unpack));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "pack_into"), ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_pack));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "unpack_from"), ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_unpack));

    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "error"), PROTO_NONE);

    const proto::ProtoObject* structClass = ctx->newObject(false);
    structClass = structClass->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__init__"), ctx->fromMethod(const_cast<proto::ProtoObject*>(structClass), py_struct_init));
    structClass = structClass->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "pack"), ctx->fromMethod(const_cast<proto::ProtoObject*>(structClass), py_struct_pack));
    structClass = structClass->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "unpack"), ctx->fromMethod(const_cast<proto::ProtoObject*>(structClass), py_struct_unpack));
    structClass = structClass->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "size"), ctx->fromInteger(1)); 

    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "Struct"), structClass);
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_clearcache"), ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_clearcache));

    return mod;
}

}
}
