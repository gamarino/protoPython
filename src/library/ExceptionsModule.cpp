#include <protoPython/PythonEnvironment.h>
#include <protoPython/DiagUtils.h>
#include <protoPython/ExceptionsModule.h>

namespace protoPython {
namespace exceptions {

static const proto::ProtoObject* exception_init(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    
    const proto::ProtoObject* instance = self;
    unsigned long startIdx = 0;
    if (!instance || instance == PROTO_NONE) {
        if (!positionalParameters || positionalParameters->getSize(context) == 0) return PROTO_NONE;
        instance = positionalParameters->getAt(context, 0);
        startIdx = 1;
    } else if (positionalParameters && positionalParameters->getSize(context) > 0) {
        // PB4: runUserClassCall prepends `obj` (the freshly-created
        // instance) as the first positional arg before invoking
        // __init__, while also passing it as `self`.  Skip that
        // duplicate.  Detect via `first.__class__ == self`: each
        // protoPython instance has __class__ pointing to itself in
        // the construction phase, so a duplicated self has its
        // `__class__` attribute equal to `self`.
        const proto::ProtoObject* first = positionalParameters->getAt(context, 0);
        if (first) {
            const proto::ProtoString* clsKey = PythonEnvironment::getInternedString(context, "__class__");
            const proto::ProtoObject* firstCls = first->getAttribute(context, clsKey);
            if (firstCls == self) {
                startIdx = 1;
            }
        }
    }

    const proto::ProtoString* argsName = PythonEnvironment::getInternedString(context, "args");
    const proto::ProtoList* actualArgs = context->newList();
    if (positionalParameters && positionalParameters->getSize(context) > startIdx) {
        for (unsigned long i = startIdx; i < positionalParameters->getSize(context); ++i) {
            actualArgs = actualArgs->appendLast(context, positionalParameters->getAt(context, i));
        }
    }
    const proto::ProtoObject* args = context->newTupleFromList(actualArgs)->asObject(context);
    instance = const_cast<proto::ProtoObject*>(instance)->setAttribute(context, argsName, args);
    instance = const_cast<proto::ProtoObject*>(instance)->setAttribute(context, PythonEnvironment::getInternedString(context, "__traceback__"), PROTO_NONE);
    instance = const_cast<proto::ProtoObject*>(instance)->setAttribute(context, PythonEnvironment::getInternedString(context, "__context__"), PROTO_NONE);
    instance = const_cast<proto::ProtoObject*>(instance)->setAttribute(context, PythonEnvironment::getInternedString(context, "__cause__"), PROTO_NONE);
    instance = const_cast<proto::ProtoObject*>(instance)->setAttribute(context, PythonEnvironment::getInternedString(context, "__suppress_context__"), PROTO_FALSE);
    return PROTO_NONE;
}

static const proto::ProtoObject* syntaxerror_init(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {

    // Call base exception init first
    exception_init(context, self, parentLink, positionalParameters, keywordParameters);

    const proto::ProtoObject* instance = self;
    if (!instance || instance == PROTO_NONE) {
        if (!positionalParameters || positionalParameters->getSize(context) == 0) return PROTO_NONE;
        instance = positionalParameters->getAt(context, 0);
    }

    // Initialize SyntaxError-specific attributes to None by default
    instance = const_cast<proto::ProtoObject*>(instance)->setAttribute(context,
        PythonEnvironment::getInternedString(context, "filename"), PROTO_NONE);
    instance = const_cast<proto::ProtoObject*>(instance)->setAttribute(context,
        PythonEnvironment::getInternedString(context, "lineno"), PROTO_NONE);
    instance = const_cast<proto::ProtoObject*>(instance)->setAttribute(context,
        PythonEnvironment::getInternedString(context, "end_lineno"), PROTO_NONE);
    instance = const_cast<proto::ProtoObject*>(instance)->setAttribute(context,
        PythonEnvironment::getInternedString(context, "offset"), PROTO_NONE);
    instance = const_cast<proto::ProtoObject*>(instance)->setAttribute(context,
        PythonEnvironment::getInternedString(context, "end_offset"), PROTO_NONE);
    instance = const_cast<proto::ProtoObject*>(instance)->setAttribute(context,
        PythonEnvironment::getInternedString(context, "text"), PROTO_NONE);
    instance = const_cast<proto::ProtoObject*>(instance)->setAttribute(context,
        PythonEnvironment::getInternedString(context, "msg"),
        PythonEnvironment::getInternedString(context, "")->asObject(context));

    // If args[1] is a tuple (filename, lineno, offset, text), unpack it (CPython convention)
    const proto::ProtoObject* argsObj = instance->getAttribute(context,
        PythonEnvironment::getInternedString(context, "args"));
    if (argsObj && (argsObj->isTuple(context) || argsObj->asList(context))) {
        unsigned long argsSize = argsObj->isTuple(context)
            ? argsObj->asTuple(context)->getSize(context)
            : argsObj->asList(context)->getSize(context);
        auto getArg = [&](unsigned long i) -> const proto::ProtoObject* {
            if (argsObj->isTuple(context)) return argsObj->asTuple(context)->getAt(context, i);
            return argsObj->asList(context)->getAt(context, i);
        };
        if (argsSize >= 1) {
            const proto::ProtoObject* msgArg = getArg(0);
            if (msgArg && msgArg != PROTO_NONE && msgArg->isString(context)) {
                instance = const_cast<proto::ProtoObject*>(instance)->setAttribute(context,
                    PythonEnvironment::getInternedString(context, "msg"), msgArg);
            }
        }
        if (argsSize >= 2) {
            const proto::ProtoObject* info = getArg(1);
            if (info && info != PROTO_NONE && (info->isTuple(context) || info->asList(context))) {
                unsigned long infoSize = info->isTuple(context)
                    ? info->asTuple(context)->getSize(context)
                    : info->asList(context)->getSize(context);
                auto getInfo = [&](unsigned long i) -> const proto::ProtoObject* {
                    if (info->isTuple(context)) return info->asTuple(context)->getAt(context, i);
                    return info->asList(context)->getAt(context, i);
                };
                if (infoSize >= 1) instance = const_cast<proto::ProtoObject*>(instance)->setAttribute(context,
                    PythonEnvironment::getInternedString(context, "filename"), getInfo(0));
                if (infoSize >= 2) instance = const_cast<proto::ProtoObject*>(instance)->setAttribute(context,
                    PythonEnvironment::getInternedString(context, "lineno"), getInfo(1));
                if (infoSize >= 3) instance = const_cast<proto::ProtoObject*>(instance)->setAttribute(context,
                    PythonEnvironment::getInternedString(context, "offset"), getInfo(2));
                if (infoSize >= 4) instance = const_cast<proto::ProtoObject*>(instance)->setAttribute(context,
                    PythonEnvironment::getInternedString(context, "text"), getInfo(3));
            }
        }
    }

    return PROTO_NONE;
}

static const proto::ProtoObject* exception_str(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    
    const proto::ProtoObject* instance = self;
    if (!instance || instance == PROTO_NONE) {
        if (!positionalParameters || positionalParameters->getSize(context) == 0) return PythonEnvironment::getInternedString(context, "")->asObject(context);
        instance = positionalParameters->getAt(context, 0);
    }
    
    const proto::ProtoString* argsName = PythonEnvironment::getInternedString(context, "args");
    const proto::ProtoObject* argsObj = instance->getAttribute(context, argsName);
    const proto::ProtoTuple* args = argsObj && argsObj->isTuple(context) ? argsObj->asTuple(context) : context->newTuple();
    
    if (get_env_diag()) {
        fprintf(stderr, "DEBUG exception_str: args size %lu\n", args->getSize(context));
    }
    
    if (args->getSize(context) == 0) {
        return PythonEnvironment::getInternedString(context, "")->asObject(context);
    }
    if (args->getSize(context) == 1) {
        const proto::ProtoObject* firstArg = args->getAt(context, 0);
        if (get_env_diag()) {
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
    
    const proto::ProtoObject* instance = self;
    if (!instance || instance == PROTO_NONE) {
        if (!positionalParameters || positionalParameters->getSize(context) == 0) return PythonEnvironment::getInternedString(context, "Exception()")->asObject(context);
        instance = positionalParameters->getAt(context, 0);
    }
    
    const proto::ProtoString* argsName = PythonEnvironment::getInternedString(context, "args");
    const proto::ProtoObject* argsObj = instance->getAttribute(context, argsName);
    const proto::ProtoTuple* args = argsObj && argsObj->isTuple(context) ? argsObj->asTuple(context) : context->newTuple();
    // Resolve the exception's actual class via getType, not via the
    // prototype chain.  An exception instance has no own __class__
    // attribute (exception_init never sets one), so a raw getAttribute
    // walk reaches the exception-class's __class__ slot, which is the
    // metaclass `type` — producing repr like "type('x not in list')".
    // env->getType returns the first parent (the actual class) and is
    // the same source of truth used by `type(e)` from user code.
    PythonEnvironment* env = PythonEnvironment::fromContext(context);
    const proto::ProtoObject* nameObj = env ? env->getType(context, instance)
                                            : instance->getAttribute(context, PythonEnvironment::getInternedString(context, "__class__"));
    if (nameObj && nameObj != PROTO_NONE) {
        nameObj = nameObj->getAttribute(context, PythonEnvironment::getInternedString(context, "__name__"));
    } else {
        nameObj = instance->getAttribute(context, PythonEnvironment::getInternedString(context, "__name__"));
    }
    
    std::string name = "Exception";
    if (nameObj && nameObj->isString(context)) {
        nameObj->asString(context)->toUTF8String(context, name);
    }
    if (args->getSize(context) == 0) {
        return PythonEnvironment::getInternedString(context, (name + "()").c_str())->asObject(context);
    }
    std::string out = name + "(";
    for (unsigned long i = 0; i < args->getSize(context) && i < 3; ++i) {
        if (i > 0) out += ", ";
        const proto::ProtoObject* a = args->getAt(context, static_cast<int>(i));
        // CPython renders each arg through repr() so containers, ints,
        // None, etc. round-trip readably:
        //   KeyError(3)         -> "KeyError(3)"
        //   ValueError([1,2])   -> "ValueError([1, 2])"
        //   ValueError(None)    -> "ValueError(None)"
        // Previously the non-string branch emitted the placeholder
        // "<obj>" which lost the key/value information at the very
        // moment a debugger or test most needs it.
        out += PythonEnvironment::reprObject(context, a);
    }
    out += ")";
    return PythonEnvironment::getInternedString(context, out.c_str())->asObject(context);
}

static const proto::ProtoObject* make_exception_type(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* objectProto,
                                                const proto::ProtoObject* typeProto,
                                                const char* name,
                                                const proto::ProtoObject* base) {
    const proto::ProtoString* py_init = PythonEnvironment::getInternedString(ctx, "__init__");
    const proto::ProtoString* py_repr = PythonEnvironment::getInternedString(ctx, "__repr__");
    const proto::ProtoString* py_str = PythonEnvironment::getInternedString(ctx, "__str__");
    const proto::ProtoString* py_name = PythonEnvironment::getInternedString(ctx, "__name__");
    const proto::ProtoString* py_call = PythonEnvironment::getInternedString(ctx, "__call__");
    const proto::ProtoString* py_class = PythonEnvironment::getInternedString(ctx, "__class__");

    const proto::ProtoObject* exc = base ? base->newChild(ctx, true) : ctx->newObject(false);
    if (get_env_diag()) {
        fprintf(stderr, "DEBUG: make_exception_type name='%s' type=%p base=%p\n", name, (void*)exc, (void*)base);
        fflush(stderr);
    }
    exc = exc->setAttribute(ctx, py_class, typeProto);
    exc = exc->setAttribute(ctx, py_name, PythonEnvironment::getInternedString(ctx, name)->asObject(ctx));
    exc = exc->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__qualname__"),
                            PythonEnvironment::getInternedString(ctx, name)->asObject(ctx));
    
    // Set __bases__ for issubclass()
    if (base) {
        const proto::ProtoList* basesList = ctx->newList()->appendLast(ctx, base);
        const proto::ProtoObject* basesTuple = ctx->newTupleFromList(basesList)->asObject(ctx);
        exc = exc->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__bases__"), basesTuple);
    } else {
        exc = exc->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__bases__"), ctx->newTuple()->asObject(ctx));
    }
    exc = exc->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__module__"), PythonEnvironment::getInternedString(ctx, "builtins")->asObject(ctx));
    exc = exc->setAttribute(ctx, py_init, ctx->fromMethod(nullptr, exception_init));
    exc = exc->setAttribute(ctx, py_repr, ctx->fromMethod(nullptr, exception_repr));
    exc = exc->setAttribute(ctx, py_str, ctx->fromMethod(nullptr, exception_str));
    // Default __cause__ / __context__ / __traceback__ to None at the class
    // level so that even unraised exception classes (used as values in
    // unittest's traceback formatter and other introspection paths) read
    // None for these attributes instead of AttributeError.  Instances
    // override these defaults when raised through an `except ... as e`
    // chain or via raise/yield-style chaining.  __suppress_context__ also
    // defaults to False (CPython's `raise X from Y` toggles it).
    exc = exc->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__cause__"),    PROTO_NONE);
    exc = exc->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__context__"),  PROTO_NONE);
    exc = exc->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__traceback__"),PROTO_NONE);
    exc = exc->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__suppress_context__"), PROTO_FALSE);

    // with_traceback(tb): sets __traceback__ and returns self
    static const auto exception_with_traceback = [](proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* self,
                                                     const proto::ParentLink*,
                                                     const proto::ProtoList* args,
                                                     const proto::ProtoSparseList*) -> const proto::ProtoObject* {
        if (!self || self == PROTO_NONE) return PROTO_NONE;
        const proto::ProtoObject* tb = (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
        const_cast<proto::ProtoObject*>(self)->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__traceback__"), tb ? tb : PROTO_NONE);
        return self;
    };
    exc = exc->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "with_traceback"),
        ctx->fromMethod(nullptr, exception_with_traceback));

    // PEP 678 add_note: append a string to self.__notes__ (creating the
    // list lazily).  Mirrors CPython 3.11+ semantics: TypeError when
    // the argument isn't a string; mutates and returns None.
    static const auto exception_add_note = [](proto::ProtoContext* ctx,
                                               const proto::ProtoObject* self,
                                               const proto::ParentLink*,
                                               const proto::ProtoList* args,
                                               const proto::ProtoSparseList*) -> const proto::ProtoObject* {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        if (!self || !args || args->getSize(ctx) < 1) return PROTO_NONE;
        const proto::ProtoObject* note = args->getAt(ctx, 0);
        if (!note || !note->isString(ctx)) {
            if (env) env->raiseTypeError(ctx, "note must be a str");
            return nullptr;
        }
        const proto::ProtoString* notesS = PythonEnvironment::getInternedString(ctx, "__notes__");
        const proto::ProtoString* dataS = env ? env->getDataString() : PythonEnvironment::getInternedString(ctx, "__data__");
        const proto::ProtoObject* notesObj = self->getAttribute(ctx, notesS);
        const proto::ProtoList* underlying = nullptr;
        if (notesObj && notesObj != PROTO_NONE) {
            // Prefer wrapped Python list __data__; fall back to raw asList.
            const proto::ProtoObject* d = notesObj->getAttribute(ctx, dataS);
            underlying = (d && d->asList(ctx)) ? d->asList(ctx) : notesObj->asList(ctx);
        }
        if (!underlying) underlying = ctx->newList();
        underlying = underlying->appendLast(ctx, note);
        // Always store as a wrapped Python list so `e.__notes__` reads
        // and indexes work from Python.
        proto::ProtoObject* wrap = const_cast<proto::ProtoObject*>(ctx->newObject(true));
        wrap = const_cast<proto::ProtoObject*>(wrap->setAttribute(ctx, dataS, underlying->asObject(ctx)));
        if (env && env->getListPrototype()) {
            wrap = const_cast<proto::ProtoObject*>(wrap->addParent(ctx, env->getListPrototype()));
            wrap = const_cast<proto::ProtoObject*>(wrap->setAttribute(ctx, env->getClassString(), env->getListPrototype()));
        }
        const_cast<proto::ProtoObject*>(self)->setAttribute(ctx, notesS, wrap);
        return PROTO_NONE;
    };
    exc = exc->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "add_note"),
        ctx->fromMethod(nullptr, exception_add_note));
    
    // Set __mro__ for attribute lookup
    const proto::ProtoList* mroList = ctx->newList()->appendLast(ctx, exc);
    if (base) {
        const proto::ProtoObject* baseMro = base->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__mro__"));
        if (baseMro && baseMro->isTuple(ctx)) {
            const proto::ProtoTuple* bt = baseMro->asTuple(ctx);
            for (size_t i = 0; i < bt->getSize(ctx); ++i) {
                mroList = mroList->appendLast(ctx, bt->getAt(ctx, i));
            }
        } else if (baseMro && baseMro->asList(ctx)) {
            const proto::ProtoList* bl = baseMro->asList(ctx);
            for (size_t i = 0; i < bl->getSize(ctx); ++i) {
                mroList = mroList->appendLast(ctx, bl->getAt(ctx, i));
            }
        } else {
            mroList = mroList->appendLast(ctx, base);
            if (base != objectProto) mroList = mroList->appendLast(ctx, objectProto);
        }
    }
    exc = exc->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__mro__"), ctx->newTupleFromList(mroList)->asObject(ctx));
    
    return exc;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx,
                                     const proto::ProtoObject* objectProto,
                                     const proto::ProtoObject* typeProto) {
    const proto::ProtoString* py_baseexception = PythonEnvironment::getInternedString(ctx, "BaseException");
    const proto::ProtoString* py_exception = PythonEnvironment::getInternedString(ctx, "Exception");
    const proto::ProtoString* py_keyerror = PythonEnvironment::getInternedString(ctx, "KeyError");
    const proto::ProtoString* py_valueerror = PythonEnvironment::getInternedString(ctx, "ValueError");
    const proto::ProtoString* py_nameerror = PythonEnvironment::getInternedString(ctx, "NameError");
    const proto::ProtoString* py_attrerror = PythonEnvironment::getInternedString(ctx, "AttributeError");
    const proto::ProtoString* py_syntaxerror = PythonEnvironment::getInternedString(ctx, "SyntaxError");
    const proto::ProtoString* py_typeerror = PythonEnvironment::getInternedString(ctx, "TypeError");
    const proto::ProtoString* py_importerror = PythonEnvironment::getInternedString(ctx, "ImportError");
    const proto::ProtoString* py_kbdinterrupt = PythonEnvironment::getInternedString(ctx, "KeyboardInterrupt");
    const proto::ProtoString* py_systemexit = PythonEnvironment::getInternedString(ctx, "SystemExit");
    const proto::ProtoString* py_recursionerror = PythonEnvironment::getInternedString(ctx, "RecursionError");
    const proto::ProtoString* py_zerodivisionerror = PythonEnvironment::getInternedString(ctx, "ZeroDivisionError");
    const proto::ProtoString* py_indexerror = PythonEnvironment::getInternedString(ctx, "IndexError");
    const proto::ProtoString* py_eoferror = PythonEnvironment::getInternedString(ctx, "EOFError");
    const proto::ProtoString* py_assertionerror = PythonEnvironment::getInternedString(ctx, "AssertionError");
    const proto::ProtoString* py_unboundlocalerror = PythonEnvironment::getInternedString(ctx, "UnboundLocalError");
    const proto::ProtoString* py_generatorexit = PythonEnvironment::getInternedString(ctx, "GeneratorExit");
    const proto::ProtoString* py_lookuperror = PythonEnvironment::getInternedString(ctx, "LookupError");
    const proto::ProtoString* py_arithmeticerror = PythonEnvironment::getInternedString(ctx, "ArithmeticError");
    const proto::ProtoString* py_stopiteration = PythonEnvironment::getInternedString(ctx, "StopIteration");
    const proto::ProtoString* py_stopasynciteration = PythonEnvironment::getInternedString(ctx, "StopAsyncIteration");
    const proto::ProtoString* py_systemerror = PythonEnvironment::getInternedString(ctx, "SystemError");
    const proto::ProtoString* py_runtimeerror = PythonEnvironment::getInternedString(ctx, "RuntimeError");
    const proto::ProtoString* py_oserror = PythonEnvironment::getInternedString(ctx, "OSError");
    const proto::ProtoString* py_blockingioerror = PythonEnvironment::getInternedString(ctx, "BlockingIOError");
    const proto::ProtoString* py_filenotfounderror = PythonEnvironment::getInternedString(ctx, "FileNotFoundError");
    const proto::ProtoString* py_permissionerror = PythonEnvironment::getInternedString(ctx, "PermissionError");
    const proto::ProtoString* py_fileexistserror = PythonEnvironment::getInternedString(ctx, "FileExistsError");
    const proto::ProtoString* py_notadirectoryerror = PythonEnvironment::getInternedString(ctx, "NotADirectoryError");
    const proto::ProtoString* py_isadirectoryerror = PythonEnvironment::getInternedString(ctx, "IsADirectoryError");
    const proto::ProtoString* py_timeouterror = PythonEnvironment::getInternedString(ctx, "TimeoutError");
    const proto::ProtoString* py_interruptederror = PythonEnvironment::getInternedString(ctx, "InterruptedError");
    const proto::ProtoString* py_childprocesserror = PythonEnvironment::getInternedString(ctx, "ChildProcessError");
    const proto::ProtoString* py_connectionerror = PythonEnvironment::getInternedString(ctx, "ConnectionError");
    const proto::ProtoString* py_brokenpipeerror = PythonEnvironment::getInternedString(ctx, "BrokenPipeError");
    const proto::ProtoString* py_modulenotfounderror = PythonEnvironment::getInternedString(ctx, "ModuleNotFoundError");
    const proto::ProtoString* py_unicodetranslateerror = PythonEnvironment::getInternedString(ctx, "UnicodeTranslateError");
    const proto::ProtoString* py_unicodeerror = PythonEnvironment::getInternedString(ctx, "UnicodeError");
    const proto::ProtoString* py_unicodeencodeerror = PythonEnvironment::getInternedString(ctx, "UnicodeEncodeError");
    const proto::ProtoString* py_unicodedecodeerror = PythonEnvironment::getInternedString(ctx, "UnicodeDecodeError");
    const proto::ProtoString* py_notimplementederror = PythonEnvironment::getInternedString(ctx, "NotImplementedError");

    const proto::ProtoString* py_warning = PythonEnvironment::getInternedString(ctx, "Warning");
    const proto::ProtoString* py_userwarning = PythonEnvironment::getInternedString(ctx, "UserWarning");
    const proto::ProtoString* py_deprecationwarning = PythonEnvironment::getInternedString(ctx, "DeprecationWarning");
    const proto::ProtoString* py_pendingdeprecationwarning = PythonEnvironment::getInternedString(ctx, "PendingDeprecationWarning");
    const proto::ProtoString* py_syntaxwarning = PythonEnvironment::getInternedString(ctx, "SyntaxWarning");
    const proto::ProtoString* py_runtimewarning = PythonEnvironment::getInternedString(ctx, "RuntimeWarning");
    const proto::ProtoString* py_futurewarning = PythonEnvironment::getInternedString(ctx, "FutureWarning");
    const proto::ProtoString* py_importwarning = PythonEnvironment::getInternedString(ctx, "ImportWarning");
    const proto::ProtoString* py_unicodewarning = PythonEnvironment::getInternedString(ctx, "UnicodeWarning");
    const proto::ProtoString* py_byteswarning = PythonEnvironment::getInternedString(ctx, "BytesWarning");
    const proto::ProtoString* py_resourcewarning = PythonEnvironment::getInternedString(ctx, "ResourceWarning");

    const proto::ProtoObject* baseExceptionType = make_exception_type(ctx, objectProto, typeProto, "BaseException", objectProto);
    const proto::ProtoObject* exceptionType = make_exception_type(ctx, objectProto, typeProto, "Exception", baseExceptionType);
    // CPython hierarchy: ArithmeticError and LookupError are intermediate
    // tiers between Exception and the concrete arithmetic / lookup errors.
    // Create them up-front so the children below inherit from the right
    // parent — previously every child was hung directly off Exception,
    // breaking `issubclass(ZeroDivisionError, ArithmeticError)` and
    // `except LookupError:` catching KeyError / IndexError.
    const proto::ProtoObject* arithmeticErrorType = make_exception_type(ctx, objectProto, typeProto, "ArithmeticError", exceptionType);
    const proto::ProtoObject* lookupErrorType = make_exception_type(ctx, objectProto, typeProto, "LookupError", exceptionType);
    const proto::ProtoObject* keyErrorType = make_exception_type(ctx, objectProto, typeProto, "KeyError", lookupErrorType);
    const proto::ProtoObject* valueErrorType = make_exception_type(ctx, objectProto, typeProto, "ValueError", exceptionType);
    const proto::ProtoObject* nameErrorType = make_exception_type(ctx, objectProto, typeProto, "NameError", exceptionType);
    const proto::ProtoObject* attributeErrorType = make_exception_type(ctx, objectProto, typeProto, "AttributeError", exceptionType);
    const proto::ProtoObject* syntaxErrorType = make_exception_type(ctx, objectProto, typeProto, "SyntaxError", exceptionType);
    syntaxErrorType = const_cast<proto::ProtoObject*>(syntaxErrorType)->setAttribute(ctx,
        PythonEnvironment::getInternedString(ctx, "__init__"),
        ctx->fromMethod(nullptr, syntaxerror_init));
    const proto::ProtoObject* typeErrorType = make_exception_type(ctx, objectProto, typeProto, "TypeError", exceptionType);
    const proto::ProtoObject* importErrorType = make_exception_type(ctx, objectProto, typeProto, "ImportError", exceptionType);
    const proto::ProtoObject* moduleNotFoundErrorType = make_exception_type(ctx, objectProto, typeProto, "ModuleNotFoundError", importErrorType);
    const proto::ProtoObject* keyboardInterruptType = make_exception_type(ctx, objectProto, typeProto, "KeyboardInterrupt", baseExceptionType);
    const proto::ProtoObject* systemExitType = make_exception_type(ctx, objectProto, typeProto, "SystemExit", baseExceptionType);
    const proto::ProtoObject* recursionErrorType = make_exception_type(ctx, objectProto, typeProto, "RecursionError", exceptionType);
    const proto::ProtoObject* zeroDivisionErrorType = make_exception_type(ctx, objectProto, typeProto, "ZeroDivisionError", arithmeticErrorType);
    const proto::ProtoObject* indexErrorType = make_exception_type(ctx, objectProto, typeProto, "IndexError", lookupErrorType);
    const proto::ProtoObject* eofErrorType = make_exception_type(ctx, objectProto, typeProto, "EOFError", exceptionType);
    const proto::ProtoObject* assertionErrorType = make_exception_type(ctx, objectProto, typeProto, "AssertionError", exceptionType);
    const proto::ProtoObject* generatorExitType = make_exception_type(ctx, objectProto, typeProto, "GeneratorExit", baseExceptionType);
    const proto::ProtoObject* unboundLocalErrorType = make_exception_type(ctx, objectProto, typeProto, "UnboundLocalError", nameErrorType);
    // arithmeticErrorType and lookupErrorType created above so KeyError /
    // IndexError / ZeroDivisionError inherit from them with the right MRO.
    const proto::ProtoObject* stopIterationType = make_exception_type(ctx, objectProto, typeProto, "StopIteration", exceptionType);
    const proto::ProtoObject* stopAsyncIterationType = make_exception_type(ctx, objectProto, typeProto, "StopAsyncIteration", exceptionType);
    const proto::ProtoObject* systemErrorType = make_exception_type(ctx, objectProto, typeProto, "SystemError", exceptionType);
    const proto::ProtoObject* runtimeErrorType = make_exception_type(ctx, objectProto, typeProto, "RuntimeError", exceptionType);
    const proto::ProtoObject* notImplementedErrorType = make_exception_type(ctx, objectProto, typeProto, "NotImplementedError", runtimeErrorType);
    const proto::ProtoObject* osErrorType = make_exception_type(ctx, objectProto, typeProto, "OSError", exceptionType);
    const proto::ProtoObject* blockingIOErrorType = make_exception_type(ctx, objectProto, typeProto, "BlockingIOError", osErrorType);
    const proto::ProtoObject* fileNotFoundErrorType = make_exception_type(ctx, objectProto, typeProto, "FileNotFoundError", osErrorType);
    const proto::ProtoObject* permissionErrorType = make_exception_type(ctx, objectProto, typeProto, "PermissionError", osErrorType);
    const proto::ProtoObject* fileExistsErrorType = make_exception_type(ctx, objectProto, typeProto, "FileExistsError", osErrorType);
    const proto::ProtoObject* notADirectoryErrorType = make_exception_type(ctx, objectProto, typeProto, "NotADirectoryError", osErrorType);
    const proto::ProtoObject* isADirectoryErrorType = make_exception_type(ctx, objectProto, typeProto, "IsADirectoryError", osErrorType);
    const proto::ProtoObject* timeoutErrorType = make_exception_type(ctx, objectProto, typeProto, "TimeoutError", osErrorType);
    const proto::ProtoObject* interruptedErrorType = make_exception_type(ctx, objectProto, typeProto, "InterruptedError", osErrorType);
    const proto::ProtoObject* childProcessErrorType = make_exception_type(ctx, objectProto, typeProto, "ChildProcessError", osErrorType);
    const proto::ProtoObject* connectionErrorType = make_exception_type(ctx, objectProto, typeProto, "ConnectionError", osErrorType);
    const proto::ProtoObject* brokenPipeErrorType = make_exception_type(ctx, objectProto, typeProto, "BrokenPipeError", connectionErrorType);

    const proto::ProtoObject* unicodeErrorType = make_exception_type(ctx, objectProto, typeProto, "UnicodeError", valueErrorType);
    const proto::ProtoObject* unicodeEncodeErrorType = make_exception_type(ctx, objectProto, typeProto, "UnicodeEncodeError", unicodeErrorType);
    const proto::ProtoObject* unicodeDecodeErrorType = make_exception_type(ctx, objectProto, typeProto, "UnicodeDecodeError", unicodeErrorType);
    const proto::ProtoObject* unicodeTranslateErrorType = make_exception_type(ctx, objectProto, typeProto, "UnicodeTranslateError", unicodeErrorType);

    const proto::ProtoObject* baseExceptionGroupType = make_exception_type(ctx, objectProto, typeProto, "BaseExceptionGroup", baseExceptionType);
    const proto::ProtoObject* exceptionGroupType = make_exception_type(ctx, objectProto, typeProto, "ExceptionGroup", exceptionType);
    const proto::ProtoObject* memoryErrorType = make_exception_type(ctx, objectProto, typeProto, "MemoryError", exceptionType);
    const proto::ProtoObject* bufferErrorType = make_exception_type(ctx, objectProto, typeProto, "BufferError", exceptionType);
    const proto::ProtoObject* overflowErrorType = make_exception_type(ctx, objectProto, typeProto, "OverflowError", arithmeticErrorType);
    const proto::ProtoObject* floatingPointErrorType = make_exception_type(ctx, objectProto, typeProto, "FloatingPointError", arithmeticErrorType);
    const proto::ProtoObject* environmentErrorType = osErrorType;  // alias
    const proto::ProtoObject* ioErrorType = osErrorType;           // alias

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
    mod = mod->setAttribute(ctx, py_baseexception, baseExceptionType);
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
    mod = mod->setAttribute(ctx, py_notimplementederror, notImplementedErrorType);
    mod = mod->setAttribute(ctx, py_indexerror, indexErrorType);
    mod = mod->setAttribute(ctx, py_eoferror, eofErrorType);
    mod = mod->setAttribute(ctx, py_assertionerror, assertionErrorType);
    mod = mod->setAttribute(ctx, py_generatorexit, generatorExitType);
    mod = mod->setAttribute(ctx, py_unboundlocalerror, unboundLocalErrorType);
    mod = mod->setAttribute(ctx, py_lookuperror, lookupErrorType);
    mod = mod->setAttribute(ctx, py_arithmeticerror, arithmeticErrorType);
    mod = mod->setAttribute(ctx, py_oserror, osErrorType);
    mod = mod->setAttribute(ctx, py_blockingioerror, blockingIOErrorType);
    mod = mod->setAttribute(ctx, py_filenotfounderror, fileNotFoundErrorType);
    mod = mod->setAttribute(ctx, py_permissionerror, permissionErrorType);
    mod = mod->setAttribute(ctx, py_fileexistserror, fileExistsErrorType);
    mod = mod->setAttribute(ctx, py_notadirectoryerror, notADirectoryErrorType);
    mod = mod->setAttribute(ctx, py_isadirectoryerror, isADirectoryErrorType);
    mod = mod->setAttribute(ctx, py_timeouterror, timeoutErrorType);
    mod = mod->setAttribute(ctx, py_interruptederror, interruptedErrorType);
    mod = mod->setAttribute(ctx, py_childprocesserror, childProcessErrorType);
    mod = mod->setAttribute(ctx, py_connectionerror, connectionErrorType);
    mod = mod->setAttribute(ctx, py_brokenpipeerror, brokenPipeErrorType);
    mod = mod->setAttribute(ctx, py_modulenotfounderror, moduleNotFoundErrorType);
    mod = mod->setAttribute(ctx, py_unicodetranslateerror, unicodeTranslateErrorType);
    mod = mod->setAttribute(ctx, py_unicodeerror, unicodeErrorType);
    mod = mod->setAttribute(ctx, py_unicodeencodeerror, unicodeEncodeErrorType);
    mod = mod->setAttribute(ctx, py_unicodedecodeerror, unicodeDecodeErrorType);
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
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "BaseExceptionGroup"), baseExceptionGroupType);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "ExceptionGroup"),     exceptionGroupType);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "MemoryError"),         memoryErrorType);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "BufferError"),         bufferErrorType);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "OverflowError"),       overflowErrorType);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "FloatingPointError"),  floatingPointErrorType);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "EnvironmentError"),    environmentErrorType);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "IOError"),             ioErrorType);

    // StopIteration custom init
    const proto::ProtoString* py_init = PythonEnvironment::getInternedString(ctx, "__init__");
    proto::ProtoObject* stopIterMutable = const_cast<proto::ProtoObject*>(stopIterationType);
    stopIterMutable->setAttribute(ctx, py_init, ctx->fromMethod(stopIterMutable, [](proto::ProtoContext* context, const proto::ProtoObject* self, const proto::ParentLink* parentLink, const proto::ProtoList* positionalParameters, const proto::ProtoSparseList* keywordParameters) -> const proto::ProtoObject* {
        // Run base init so `self.args` is populated, then read value
        // back from `self.args` (which has the duplicate-self already
        // stripped if applicable).  This avoids the historical bug
        // where StopIteration.value held `(class_obj, real_value)`
        // because the caller leaked the class as posArgs[0].
        exception_init(context, self, parentLink, positionalParameters, keywordParameters);
        const proto::ProtoObject* args = self->getAttribute(context,
            PythonEnvironment::getInternedString(context, "args"));
        const proto::ProtoObject* value = PROTO_NONE;
        if (args && args->isTuple(context)) {
            const proto::ProtoTuple* t = args->asTuple(context);
            unsigned long n = t->getSize(context);
            if (n == 1) value = t->getAt(context, 0);
            else if (n > 1) value = args;       // tuple of values
        }
        self = self->setAttribute(context, PythonEnvironment::getInternedString(context, "value"), value);
        return PROTO_NONE;
    }));

    mod = mod->setAttribute(ctx, py_stopiteration, stopIterationType);
    mod = mod->setAttribute(ctx, py_stopasynciteration, stopAsyncIterationType);
    mod = mod->setAttribute(ctx, py_systemerror, systemErrorType);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__name__"), PythonEnvironment::getInternedString(ctx, "exceptions")->asObject(ctx));

    return mod;
}

} // namespace exceptions
} // namespace protoPython
