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
    const proto::ProtoList* actualArgs = context->newList();
    if (positionalParameters && positionalParameters->getSize(context) > 0) {
        for (unsigned long i = 0; i < positionalParameters->getSize(context); ++i) {
            actualArgs = actualArgs->appendLast(context, positionalParameters->getAt(context, i));
        }
    }
    const proto::ProtoObject* args = context->newTupleFromList(actualArgs)->asObject(context);
    self = self->setAttribute(context, argsName, args);
    self = self->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__traceback__"), PROTO_NONE);
    return PROTO_NONE;
}

static const proto::ProtoObject* exception_call(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoObject* instance = self->newChild(context, true);
    instance = instance->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__class__"), self);
    const proto::ProtoString* argsName = proto::ProtoString::fromUTF8String(context, "args");
    
    const proto::ProtoList* actualArgs = context->newList();
    if (positionalParameters) {
        for (unsigned long i = 0; i < positionalParameters->getSize(context); ++i) {
            // Skip the class object itself if passed as the first argument due to how __call__ is bound
            if (i == 0 && positionalParameters->getAt(context, i) == self) continue;
            actualArgs = actualArgs->appendLast(context, positionalParameters->getAt(context, i));
        }
    }
    const proto::ProtoObject* args = context->newTupleFromList(actualArgs)->asObject(context);
    instance = instance->setAttribute(context, argsName, args);
    instance = instance->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__traceback__"), PROTO_NONE);
    const proto::ProtoObject* init = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__init__"));
    if (init && init->asMethod(context)) {
        init->asMethod(context)(context, instance, nullptr, actualArgs, keywordParameters);
    }
    return instance;
}

static const proto::ProtoObject* exception_str(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    const proto::ProtoString* argsName = proto::ProtoString::fromUTF8String(context, "args");
    const proto::ProtoObject* argsObj = self->getAttribute(context, argsName);
    const proto::ProtoTuple* args = argsObj && argsObj->isTuple(context) ? argsObj->asTuple(context) : context->newTuple();
    
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG exception_str: args size %lu\n", args->getSize(context));
    }
    
    if (args->getSize(context) == 0) {
        return context->fromUTF8String("");
    }
    if (args->getSize(context) == 1) {
        const proto::ProtoObject* firstArg = args->getAt(context, 0);
        if (std::getenv("PROTO_ENV_DIAG")) {
            fprintf(stderr, "DEBUG exception_str: returning firstArg=%p isString=%d\n", (void*)firstArg, firstArg->isString(context));
        }
        return firstArg;
    }
    return args->asObject(context);
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
    const proto::ProtoString* py_str = proto::ProtoString::fromUTF8String(ctx, "__str__");
    const proto::ProtoString* py_name = proto::ProtoString::fromUTF8String(ctx, "__name__");
    const proto::ProtoString* py_call = proto::ProtoString::fromUTF8String(ctx, "__call__");
    const proto::ProtoString* py_class = proto::ProtoString::fromUTF8String(ctx, "__class__");

    const proto::ProtoObject* exc = base ? base->newChild(ctx, true) : ctx->newObject(false);
    if (std::getenv("PROTO_ENV_DIAG")) {
        fprintf(stderr, "DEBUG: make_exception_type name='%s' type=%p base=%p\n", name, (void*)exc, (void*)base);
        fflush(stderr);
    }
    exc = exc->setAttribute(ctx, py_class, typeProto);
    exc = exc->setAttribute(ctx, py_name, ctx->fromUTF8String(name));
    
    // Set __bases__ for issubclass()
    if (base) {
        const proto::ProtoList* basesList = ctx->newList()->appendLast(ctx, base);
        const proto::ProtoObject* basesTuple = ctx->newTupleFromList(basesList)->asObject(ctx);
        exc = exc->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__bases__"), basesTuple);
    } else {
        exc = exc->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__bases__"), ctx->newTuple()->asObject(ctx));
    }
    exc = exc->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__module__"), ctx->fromUTF8String("builtins"));
    exc = exc->setAttribute(ctx, py_init, ctx->fromMethod(const_cast<proto::ProtoObject*>(exc), exception_init));
    exc = exc->setAttribute(ctx, py_repr, ctx->fromMethod(const_cast<proto::ProtoObject*>(exc), exception_repr));
    exc = exc->setAttribute(ctx, py_str, ctx->fromMethod(const_cast<proto::ProtoObject*>(exc), exception_str));
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
    const proto::ProtoString* py_stopasynciteration = proto::ProtoString::fromUTF8String(ctx, "StopAsyncIteration");
    const proto::ProtoString* py_systemerror = proto::ProtoString::fromUTF8String(ctx, "SystemError");
    const proto::ProtoString* py_runtimeerror = proto::ProtoString::fromUTF8String(ctx, "RuntimeError");
    const proto::ProtoString* py_oserror = proto::ProtoString::fromUTF8String(ctx, "OSError");
    const proto::ProtoString* py_blockingioerror = proto::ProtoString::fromUTF8String(ctx, "BlockingIOError");

    const proto::ProtoString* py_warning = proto::ProtoString::fromUTF8String(ctx, "Warning");
    const proto::ProtoString* py_userwarning = proto::ProtoString::fromUTF8String(ctx, "UserWarning");
    const proto::ProtoString* py_deprecationwarning = proto::ProtoString::fromUTF8String(ctx, "DeprecationWarning");
    const proto::ProtoString* py_pendingdeprecationwarning = proto::ProtoString::fromUTF8String(ctx, "PendingDeprecationWarning");
    const proto::ProtoString* py_syntaxwarning = proto::ProtoString::fromUTF8String(ctx, "SyntaxWarning");
    const proto::ProtoString* py_runtimewarning = proto::ProtoString::fromUTF8String(ctx, "RuntimeWarning");
    const proto::ProtoString* py_futurewarning = proto::ProtoString::fromUTF8String(ctx, "FutureWarning");
    const proto::ProtoString* py_importwarning = proto::ProtoString::fromUTF8String(ctx, "ImportWarning");
    const proto::ProtoString* py_unicodewarning = proto::ProtoString::fromUTF8String(ctx, "UnicodeWarning");
    const proto::ProtoString* py_byteswarning = proto::ProtoString::fromUTF8String(ctx, "BytesWarning");
    const proto::ProtoString* py_resourcewarning = proto::ProtoString::fromUTF8String(ctx, "ResourceWarning");

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
    const proto::ProtoObject* stopAsyncIterationType = make_exception_type(ctx, objectProto, typeProto, "StopAsyncIteration", exceptionType);
    const proto::ProtoObject* systemErrorType = make_exception_type(ctx, objectProto, typeProto, "SystemError", exceptionType);
    const proto::ProtoObject* runtimeErrorType = make_exception_type(ctx, objectProto, typeProto, "RuntimeError", exceptionType);
    const proto::ProtoObject* osErrorType = make_exception_type(ctx, objectProto, typeProto, "OSError", exceptionType);
    const proto::ProtoObject* blockingIOErrorType = make_exception_type(ctx, objectProto, typeProto, "BlockingIOError", osErrorType);

    const proto::ProtoObject* warningType = make_exception_type(ctx, objectProto, typeProto, "Warning", exceptionType);
    const proto::ProtoObject* userWarningType = make_exception_type(ctx, objectProto, typeProto, "UserWarning", warningType);
    const proto::ProtoObject* deprecationWarningType = make_exception_type(ctx, objectProto, typeProto, "DeprecationWarning", warningType);
    const proto::ProtoObject* pendingDeprecationWarningType = make_exception_type(ctx, objectProto, typeProto, "PendingDeprecationWarning", warningType);
    const proto::ProtoObject* syntaxWarningType = make_exception_type(ctx, objectProto, typeProto, "SyntaxWarning", warningType);
    const proto::ProtoObject* runtimeWarningType = make_exception_type(ctx, objectProto, typeProto, "RuntimeWarning", warningType);
    const proto::ProtoObject* futureWarningType = make_exception_type(ctx, objectProto, typeProto, "FutureWarning", warningType);
    const proto::ProtoObject* importWarningType = make_exception_type(ctx, objectProto, typeProto, "ImportWarning", warningType);
    const proto::ProtoObject* unicodeWarningType = make_exception_type(ctx, objectProto, typeProto, "UnicodeWarning", warningType);
    const proto::ProtoObject* bytesWarningType = make_exception_type(ctx, objectProto, typeProto, "BytesWarning", warningType);
    const proto::ProtoObject* resourceWarningType = make_exception_type(ctx, objectProto, typeProto, "ResourceWarning", warningType);

    const proto::ProtoObject* mod = ctx->newObject(false);
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
    mod = mod->setAttribute(ctx, py_runtimeerror, runtimeErrorType);
    mod = mod->setAttribute(ctx, py_indexerror, indexErrorType);
    mod = mod->setAttribute(ctx, py_eoferror, eofErrorType);
    mod = mod->setAttribute(ctx, py_assertionerror, assertionErrorType);
    mod = mod->setAttribute(ctx, py_oserror, osErrorType);
    mod = mod->setAttribute(ctx, py_blockingioerror, blockingIOErrorType);
    mod = mod->setAttribute(ctx, py_warning, warningType);
    mod = mod->setAttribute(ctx, py_userwarning, userWarningType);
    mod = mod->setAttribute(ctx, py_deprecationwarning, deprecationWarningType);
    mod = mod->setAttribute(ctx, py_pendingdeprecationwarning, pendingDeprecationWarningType);
    mod = mod->setAttribute(ctx, py_syntaxwarning, syntaxWarningType);
    mod = mod->setAttribute(ctx, py_runtimewarning, runtimeWarningType);
    mod = mod->setAttribute(ctx, py_futurewarning, futureWarningType);
    mod = mod->setAttribute(ctx, py_importwarning, importWarningType);
    mod = mod->setAttribute(ctx, py_unicodewarning, unicodeWarningType);
    mod = mod->setAttribute(ctx, py_byteswarning, bytesWarningType);
    mod = mod->setAttribute(ctx, py_resourcewarning, resourceWarningType);

    // StopIteration custom init
    const proto::ProtoString* py_init = proto::ProtoString::fromUTF8String(ctx, "__init__");
    proto::ProtoObject* stopIterMutable = const_cast<proto::ProtoObject*>(stopIterationType);
    stopIterMutable->setAttribute(ctx, py_init, ctx->fromMethod(stopIterMutable, [](proto::ProtoContext* context, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* positionalParameters, const proto::ProtoSparseList* keywordParameters) -> const proto::ProtoObject* {
        exception_init(context, self, parentLink, positionalParameters, keywordParameters);
        const proto::ProtoObject* value = PROTO_NONE;
        if (positionalParameters && positionalParameters->getSize(context) > 0) {
            if (positionalParameters->getSize(context) == 1) {
                value = positionalParameters->getAt(context, 0);
            } else {
                 value = context->newTupleFromList(positionalParameters)->asObject(context);
            }
        }
        self = self->setAttribute(context, proto::ProtoString::fromUTF8String(context, "value"), value);
        return PROTO_NONE;
    }));

    mod = mod->setAttribute(ctx, py_stopiteration, stopIterationType);
    mod = mod->setAttribute(ctx, py_stopasynciteration, stopAsyncIterationType);
    mod = mod->setAttribute(ctx, py_systemerror, systemErrorType);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__name__"), ctx->fromUTF8String("exceptions"));

    return mod;
}

} // namespace exceptions
} // namespace protoPython
