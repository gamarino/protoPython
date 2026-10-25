#include <protoPython/OpcodeModule.h>
#include <protoPython/Compiler.h>
#include <protoPython/Disassembler.h>
#include <protoPython/ExecutionEngine.h>
#include <protoPython/PythonEnvironment.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace protoPython {
namespace opcode_module {

static const proto::ProtoObject* py_has_arg(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments*) {
    if (args->getSize(ctx) < 1) return PROTO_FALSE;
    const proto::ProtoObject* opObj = args->getAt(ctx, 0);
    if (!opObj->isInteger(ctx)) return PROTO_FALSE;
    int op = static_cast<int>(opObj->asLong(ctx));
    
    // The opcodes whose argument word carries an operand (docs/VM.md, 2).
    switch (op) {
        case OP_LOAD_CONST: case OP_LOAD_NAME: case OP_STORE_NAME:
        case OP_CALL_FUNCTION: case OP_COMPARE_OP: case OP_POP_JUMP_IF_FALSE:
        case OP_POP_JUMP_IF_TRUE: case OP_JUMP_ABSOLUTE: case OP_JUMP_FORWARD:
        case OP_LOAD_ATTR: case OP_STORE_ATTR: case OP_BUILD_LIST:
        case OP_BUILD_MAP: case OP_BUILD_TUPLE: case OP_UNPACK_SEQUENCE:
        case OP_LOAD_GLOBAL: case OP_STORE_GLOBAL: case OP_BUILD_SLICE:
        case OP_FOR_ITER: case OP_LIST_APPEND: case OP_MAP_ADD:
        case OP_SET_ADD: case OP_DICT_UPDATE: case OP_LIST_EXTEND:
        case OP_DICT_MERGE:
        case OP_SET_UPDATE: case OP_BUILD_SET: case OP_BUILD_STRING:
        case OP_LOAD_DEREF: case OP_STORE_DEREF: case OP_DELETE_DEREF:
        case OP_LOAD_CLOSURE: case OP_LOAD_CLASSDEREF: case OP_SETUP_FINALLY:
        case OP_SETUP_WITH: case OP_SETUP_ASYNC_WITH: case OP_RERAISE:
        case OP_GEN_START: case OP_FORMAT_VALUE: case OP_EXTENDED_ARG:
        case OP_MATCH_MAPPING: case OP_MATCH_SEQUENCE: case OP_RAISE_VARARGS:
        case OP_LOAD_FAST: case OP_STORE_FAST: case OP_DELETE_NAME:
        case OP_DELETE_ATTR: case OP_DELETE_SUBSCR: case OP_DELETE_GLOBAL:
        case OP_DELETE_FAST: case OP_UNPACK_EX: case OP_GET_LEN:
        case OP_BUILD_FUNCTION: case OP_CALL_FUNCTION_KW: case OP_CALL_FUNCTION_EX:
        case OP_IMPORT_FROM: case OP_STORE_ATTR_GROUP: case OP_STORE_ATTR_GROUP_END:
        case OP_ACC_FAST_FAST: case OP_INC_FAST_K: case OP_LT_FAST_FAST_JF:
            return PROTO_TRUE;
        default:
            return PROTO_FALSE;
    }
}

// stack_effect(opcode, oparg=None, *, jump=None): the instruction's net effect
// on the operand stack (Compiler.h, opcodeStackEffect).  `jump` selects the
// branch of FOR_ITER (False: a value pushed, True: the exhausted iterator
// popped) and of SETUP_FINALLY / SETUP_WITH (True: the handler entry, which
// receives `None, exc, exc` / `exc`); None gives the larger of the two, as
// CPython's does.  ValueError for an unknown opcode, or for an oparg given to
// an opcode that takes none or missing for one that needs it.
static const proto::ProtoObject* py_stack_effect(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments* kwargs) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const unsigned long n = args ? static_cast<unsigned long>(args->getSize(ctx)) : 0;
    auto keyword = [&](const char* name) -> const proto::ProtoObject* {
        if (!kwargs) return nullptr;
        const proto::proto_ulong h = PythonEnvironment::getInternedString(ctx, name)->getHash(ctx);
        return kwargs->has(ctx, h) ? kwargs->getAt(ctx, h) : nullptr;
    };
    if (n < 1 || n > 2) {
        env->raiseTypeError(ctx, "stack_effect expected 1 or 2 positional arguments, got " + std::to_string(n));
        return nullptr;
    }
    const proto::ProtoObject* opObj = args->getAt(ctx, 0);
    const proto::ProtoObject* argObj = n > 1 ? args->getAt(ctx, 1) : keyword("oparg");
    if (argObj == PROTO_NONE) argObj = nullptr;
    const proto::ProtoObject* jumpObj = keyword("jump");
    if (!opObj->isInteger(ctx) || (argObj && !argObj->isInteger(ctx))) {
        env->raiseTypeError(ctx, "stack_effect: opcode and oparg must be integers");
        return nullptr;
    }
    const long long op = opObj->asLong(ctx);
    if (op < 0 || op > 255 || !opcodeName(static_cast<int>(op))) {
        env->raiseValueError(ctx, PythonEnvironment::getInternedString(ctx, "invalid opcode or oparg")->asObject(ctx));
        return nullptr;
    }
    const bool hasArg = py_has_arg(ctx, nullptr, nullptr, args, nullptr) == PROTO_TRUE;
    if (hasArg && !argObj) {
        env->raiseValueError(ctx, PythonEnvironment::getInternedString(ctx,
            "stack_effect: opcode requires oparg but oparg was not specified")->asObject(ctx));
        return nullptr;
    }
    if (!hasArg && argObj) {
        env->raiseValueError(ctx, PythonEnvironment::getInternedString(ctx,
            "stack_effect: opcode does not permit oparg but oparg was specified")->asObject(ctx));
        return nullptr;
    }
    const int arg = argObj ? static_cast<int>(argObj->asLong(ctx)) : 0;
    int effect = opcodeStackEffect(static_cast<int>(op), arg);
    int jumpEffect = effect;
    switch (op) {
        case OP_FOR_ITER: jumpEffect = -1; break;
        case OP_SETUP_FINALLY: jumpEffect = 3; break;
        case OP_SETUP_WITH: jumpEffect = 0; break;   // mgr -> exc
        default: break;
    }
    if (jumpObj == PROTO_TRUE) effect = jumpEffect;
    else if (!jumpObj || jumpObj == PROTO_NONE) effect = std::max(effect, jumpEffect);
    return ctx->fromInteger(effect);
}

// Dummy stubs to satisfy opcode.py
static const proto::ProtoObject* py_null_list_stub(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoArguments*,
    const proto::ProtoKeywordArguments*) {
    return PythonEnvironment::wrapList(ctx, ctx->newList());
}

static const proto::ProtoObject* py_false_stub(proto::ProtoContext*, const proto::ProtoObject*, const proto::ParentLink*, const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) { return PROTO_FALSE; }

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    PythonEnvironment* env = PythonEnvironment::get(ctx);
    const proto::ProtoObject* mod = ctx->newObject(false);
    if (env && env->getObjectPrototype()) mod = mod->addParent(ctx, env->getObjectPrototype());
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__name__"), PythonEnvironment::getInternedString(ctx, "_opcode")->asObject(ctx));

    // Every opcode of ExecutionEngine.h, named as the disassembler names it,
    // plus CPython 3.12's name for LOAD_CLASSDEREF in opmap.
    std::map<std::string, int> opmap_data;
    for (int op = 0; op <= 255; ++op)
        if (const char* name = opcodeName(op)) opmap_data[name] = op;
    std::map<std::string, int> opmap_aliases = {{"LOAD_FROM_DICT_OR_DEREF", OP_LOAD_CLASSDEREF}};

    const proto::ProtoObject* opmap = nullptr;
    const proto::ProtoObject* dictClass = env ? env->resolve("dict", ctx) : nullptr;
    if (env && dictClass && dictClass != PROTO_NONE) {
        opmap = env->callObject(dictClass, {});
    } else {
        opmap = ctx->newObject(false);
        if (env && env->getDictPrototype()) opmap = opmap->addParent(ctx, env->getDictPrototype());
        if (env) opmap = env->initDictStorage(ctx, opmap);
    }
    
    int maxOp = 255;
    std::map<std::string, int> opmap_all = opmap_data;
    opmap_all.insert(opmap_aliases.begin(), opmap_aliases.end());
    for (const auto& pair : opmap_all) {
        const proto::ProtoString* k = proto::ProtoString::createSymbol(ctx, pair.first.c_str());
        if (env && opmap) {
            env->setItem(opmap, k->asObject(ctx), ctx->fromInteger(pair.second));
        } else {
            opmap = opmap->setAttribute(ctx, k, ctx->fromInteger(pair.second));
        }
        if (pair.second > maxOp) maxOp = pair.second;
    }
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "opmap"), opmap);

    const proto::ProtoList* opname_list = ctx->newList();
    std::vector<std::string> namesVec(maxOp + 1, "");
    for (const auto& pair : opmap_data) {
        namesVec[pair.second] = pair.first;
    }
    for (int i = 0; i <= maxOp; ++i) {
        const proto::ProtoObject* nameObj = nullptr;
        if (namesVec[i].empty()) {
            nameObj = PythonEnvironment::getInternedString(ctx, ("<" + std::to_string(i) + ">").c_str())->asObject(ctx);
        } else {
            nameObj = PythonEnvironment::getInternedString(ctx, namesVec[i].c_str())->asObject(ctx);
        }
        if (env && env->getStrPrototype()) nameObj = nameObj->addParent(ctx, env->getStrPrototype());
        opname_list = opname_list->appendLast(ctx, nameObj);
    }

    proto::ProtoObject* opname_obj = const_cast<proto::ProtoObject*>(ctx->newObject(false));
    if (env && env->getListPrototype()) {
        opname_obj = const_cast<proto::ProtoObject*>(opname_obj->addParent(ctx, env->getListPrototype()));
        opname_obj = const_cast<proto::ProtoObject*>(opname_obj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__class__"), env->getListPrototype()));
    }
    opname_obj = const_cast<proto::ProtoObject*>(opname_obj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__data__"), opname_list->asObject(ctx)));

    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "opname"), opname_obj);

    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "stack_effect"), ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_stack_effect));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "has_arg"), ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_has_arg));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "has_const"), ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_false_stub));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "has_name"), ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_false_stub));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "has_jump"), ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_false_stub));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "has_free"), ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_false_stub));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "has_local"), ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_false_stub));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "has_exc"), ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_false_stub));
    
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "get_intrinsic1_descs"), ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_null_list_stub));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "get_intrinsic2_descs"), ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_null_list_stub));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "get_special_method_names"), ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_null_list_stub));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "get_nb_ops"), ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_null_list_stub));

    return mod;
}

} // namespace opcode_module
} // namespace protoPython
