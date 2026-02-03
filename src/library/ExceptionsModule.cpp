#include <protoPython/ExceptionsModule.h>

namespace protoPython {
namespace exceptions {

static const proto::ProtoObject* exception_init(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* argsName = proto::ProtoString::fromUTF8String(context, "__args__");
    const proto::ProtoObject* args = positionalParameters && positionalParameters->getSize(context) > 0
        ? positionalParameters->asObject(context) : context->newList()->asObject(context);
    self->setAttribute(context, argsName, args);
    return PROTO_NONE;
}

static const proto::ProtoObject* exception_call(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoObject* instance = self->newChild(context, true);
    const proto::ProtoString* argsName = proto::ProtoString::fromUTF8String(context, "__args__");
    const proto::ProtoObject* args = positionalParameters ? positionalParameters->asObject(context) : context->newList()->asObject(context);
    instance->setAttribute(context, argsName, args);
    const proto::ProtoObject* init = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__init__"));
    if (init && init->asMethod(context)) {
        init->asMethod(context)(context, instance, nullptr, positionalParameters ? positionalParameters : context->newList(), keywordParameters);
    }
    return instance;
}

static const proto::ProtoObject* exception_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* argsName = proto::ProtoString::fromUTF8String(context, "__args__");
    const proto::ProtoObject* argsObj = self->getAttribute(context, argsName);
    const proto::ProtoList* args = argsObj && argsObj->asList(context) ? argsObj->asList(context) : context->newList();
    const proto::ProtoObject* nameObj = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__name__"));
    std::string name = "Exception";
    if (nameObj && nameObj->isString(context)) {
        nameObj->asString(context)->toUTF8String(context, name);
    }
    if (args->getSize(context) == 0) {
        return context->fromUTF8String(name.c_str());
    }
    std::string out = name + "(";
    for (unsigned long i = 0; i < args->getSize(context) && i < 3; ++i) {
        if (i > 0) out += ", ";
        const proto::ProtoObject* a = args->getAt(context, static_cast<int>(i));
        if (a->isString(context)) {
            std::string s;
            a->asString(context)->toUTF8String(context, s);
            out += "'" + s + "'";
        } else {
            out += "<obj>";
        }
    }
    out += ")";
    return context->fromUTF8String(out.c_str());
}

static const proto::ProtoObject* make_exception_type(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* objectProto,
                                                const proto::ProtoObject* typeProto,
                                                const char* name,
                                                const proto::ProtoObject* base) {
    const proto::ProtoString* py_init = proto::ProtoString::fromUTF8String(ctx, "__init__");
    const proto::ProtoString* py_repr = proto::ProtoString::fromUTF8String(ctx, "__repr__");
    const proto::ProtoString* py_name = proto::ProtoString::fromUTF8String(ctx, "__name__");
    const proto::ProtoString* py_call = proto::ProtoString::fromUTF8String(ctx, "__call__");
    const proto::ProtoString* py_class = proto::ProtoString::fromUTF8String(ctx, "__class__");

    const proto::ProtoObject* exc = ctx->newObject(true);
    exc = exc->addParent(ctx, base);
    exc = exc->setAttribute(ctx, py_class, typeProto);
    exc = exc->setAttribute(ctx, py_name, ctx->fromUTF8String(name));
    exc = exc->setAttribute(ctx, py_init, ctx->fromMethod(const_cast<proto::ProtoObject*>(exc), exception_init));
    exc = exc->setAttribute(ctx, py_repr, ctx->fromMethod(const_cast<proto::ProtoObject*>(exc), exception_repr));
    exc = exc->setAttribute(ctx, py_call, ctx->fromMethod(const_cast<proto::ProtoObject*>(exc), exception_call));
    return exc;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx,
                                     const proto::ProtoObject* objectProto,
                                     const proto::ProtoObject* typeProto) {
    const proto::ProtoString* py_exception = proto::ProtoString::fromUTF8String(ctx, "Exception");
    const proto::ProtoString* py_keyerror = proto::ProtoString::fromUTF8String(ctx, "KeyError");
    const proto::ProtoString* py_valueerror = proto::ProtoString::fromUTF8String(ctx, "ValueError");

    const proto::ProtoObject* exceptionType = make_exception_type(ctx, objectProto, typeProto, "Exception", objectProto);
    const proto::ProtoObject* keyErrorType = make_exception_type(ctx, objectProto, typeProto, "KeyError", exceptionType);
    const proto::ProtoObject* valueErrorType = make_exception_type(ctx, objectProto, typeProto, "ValueError", exceptionType);

    const proto::ProtoObject* mod = ctx->newObject(true);
    mod = mod->setAttribute(ctx, py_exception, exceptionType);
    mod = mod->setAttribute(ctx, py_keyerror, keyErrorType);
    mod = mod->setAttribute(ctx, py_valueerror, valueErrorType);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__name__"), ctx->fromUTF8String("exceptions"));

    return mod;
}

} // namespace exceptions
} // namespace protoPython
