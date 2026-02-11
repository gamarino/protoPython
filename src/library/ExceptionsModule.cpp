#include <protoPython/ExceptionsModule.h>

namespace protoPython {
namespace exceptions {

static const proto::ProtoObject* exception_init(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* argsName = proto::ProtoString::fromUTF8String(context, "args");
    const proto::ProtoObject* args = (positionalParameters && positionalParameters->getSize(context) > 0)
        ? context->newTupleFromList(positionalParameters)->asObject(context)
        : context->newTuple()->asObject(context);
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
    instance->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__class__"), self);
    const proto::ProtoString* argsName = proto::ProtoString::fromUTF8String(context, "args");
    const proto::ProtoObject* args = positionalParameters 
        ? context->newTupleFromList(positionalParameters)->asObject(context) 
        : context->newTuple()->asObject(context);
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
    const proto::ProtoString* argsName = proto::ProtoString::fromUTF8String(context, "args");
    const proto::ProtoObject* argsObj = self->getAttribute(context, argsName);
    const proto::ProtoTuple* args = argsObj && argsObj->isTuple(context) ? argsObj->asTuple(context) : context->newTuple();
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
    const proto::ProtoString* py_nameerror = proto::ProtoString::fromUTF8String(ctx, "NameError");
    const proto::ProtoString* py_attrerror = proto::ProtoString::fromUTF8String(ctx, "AttributeError");
    const proto::ProtoString* py_syntaxerror = proto::ProtoString::fromUTF8String(ctx, "SyntaxError");
    const proto::ProtoString* py_typeerror = proto::ProtoString::fromUTF8String(ctx, "TypeError");
    const proto::ProtoString* py_importerror = proto::ProtoString::fromUTF8String(ctx, "ImportError");
    const proto::ProtoString* py_kbdinterrupt = proto::ProtoString::fromUTF8String(ctx, "KeyboardInterrupt");
    const proto::ProtoString* py_systemexit = proto::ProtoString::fromUTF8String(ctx, "SystemExit");
    const proto::ProtoString* py_recursionerror = proto::ProtoString::fromUTF8String(ctx, "RecursionError");
    const proto::ProtoString* py_zerodivisionerror = proto::ProtoString::fromUTF8String(ctx, "ZeroDivisionError");
    const proto::ProtoString* py_indexerror = proto::ProtoString::fromUTF8String(ctx, "IndexError");
    const proto::ProtoString* py_eoferror = proto::ProtoString::fromUTF8String(ctx, "EOFError");
    const proto::ProtoString* py_assertionerror = proto::ProtoString::fromUTF8String(ctx, "AssertionError");
    const proto::ProtoString* py_stopiteration = proto::ProtoString::fromUTF8String(ctx, "StopIteration");

    const proto::ProtoObject* exceptionType = make_exception_type(ctx, objectProto, typeProto, "Exception", objectProto);
    const proto::ProtoObject* keyErrorType = make_exception_type(ctx, objectProto, typeProto, "KeyError", exceptionType);
    const proto::ProtoObject* valueErrorType = make_exception_type(ctx, objectProto, typeProto, "ValueError", exceptionType);
    const proto::ProtoObject* nameErrorType = make_exception_type(ctx, objectProto, typeProto, "NameError", exceptionType);
    const proto::ProtoObject* attributeErrorType = make_exception_type(ctx, objectProto, typeProto, "AttributeError", exceptionType);
    const proto::ProtoObject* syntaxErrorType = make_exception_type(ctx, objectProto, typeProto, "SyntaxError", exceptionType);
    const proto::ProtoObject* typeErrorType = make_exception_type(ctx, objectProto, typeProto, "TypeError", exceptionType);
    const proto::ProtoObject* importErrorType = make_exception_type(ctx, objectProto, typeProto, "ImportError", exceptionType);
    const proto::ProtoObject* keyboardInterruptType = make_exception_type(ctx, objectProto, typeProto, "KeyboardInterrupt", exceptionType);
    const proto::ProtoObject* systemExitType = make_exception_type(ctx, objectProto, typeProto, "SystemExit", exceptionType);
    const proto::ProtoObject* recursionErrorType = make_exception_type(ctx, objectProto, typeProto, "RecursionError", exceptionType);
    const proto::ProtoObject* zeroDivisionErrorType = make_exception_type(ctx, objectProto, typeProto, "ZeroDivisionError", exceptionType);
    const proto::ProtoObject* indexErrorType = make_exception_type(ctx, objectProto, typeProto, "IndexError", exceptionType);
    const proto::ProtoObject* eofErrorType = make_exception_type(ctx, objectProto, typeProto, "EOFError", exceptionType);
    const proto::ProtoObject* assertionErrorType = make_exception_type(ctx, objectProto, typeProto, "AssertionError", exceptionType);
    const proto::ProtoObject* stopIterationType = make_exception_type(ctx, objectProto, typeProto, "StopIteration", exceptionType);

    const proto::ProtoObject* mod = ctx->newObject(true);
    mod = mod->setAttribute(ctx, py_exception, exceptionType);
    mod = mod->setAttribute(ctx, py_keyerror, keyErrorType);
    mod = mod->setAttribute(ctx, py_valueerror, valueErrorType);
    mod = mod->setAttribute(ctx, py_nameerror, nameErrorType);
    mod = mod->setAttribute(ctx, py_attrerror, attributeErrorType);
    mod = mod->setAttribute(ctx, py_syntaxerror, syntaxErrorType);
    mod = mod->setAttribute(ctx, py_typeerror, typeErrorType);
    mod = mod->setAttribute(ctx, py_importerror, importErrorType);
    mod = mod->setAttribute(ctx, py_kbdinterrupt, keyboardInterruptType);
    mod = mod->setAttribute(ctx, py_systemexit, systemExitType);
    mod = mod->setAttribute(ctx, py_recursionerror, recursionErrorType);
    mod = mod->setAttribute(ctx, py_zerodivisionerror, zeroDivisionErrorType);
    mod = mod->setAttribute(ctx, py_indexerror, indexErrorType);
    mod = mod->setAttribute(ctx, py_eoferror, eofErrorType);
    mod = mod->setAttribute(ctx, py_assertionerror, assertionErrorType);
    mod = mod->setAttribute(ctx, py_stopiteration, stopIterationType);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__name__"), ctx->fromUTF8String("exceptions"));

    return mod;
}

} // namespace exceptions
} // namespace protoPython
