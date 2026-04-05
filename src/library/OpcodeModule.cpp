#include <protoPython/OpcodeModule.h>
#include <protoPython/ExecutionEngine.h>
#include <protoPython/PythonEnvironment.h>
#include <map>
#include <string>
#include <vector>

namespace protoPython {
namespace opcode_module {

static const proto::ProtoObject* py_has_arg(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (args->getSize(ctx) < 1) return PROTO_FALSE;
    const proto::ProtoObject* opObj = args->getAt(ctx, 0);
    if (!opObj->isInteger(ctx)) return PROTO_FALSE;
    int op = static_cast<int>(opObj->asLong(ctx));
    
    // Simplistic check based on ExecutionEngine.cpp logic
    switch (op) {
        case OP_LOAD_CONST: case OP_LOAD_NAME: case OP_STORE_NAME:
        case OP_CALL_FUNCTION: case OP_COMPARE_OP: case OP_POP_JUMP_IF_FALSE:
        case OP_POP_JUMP_IF_TRUE: case OP_JUMP_ABSOLUTE: case OP_JUMP_FORWARD:
        case OP_LOAD_ATTR: case OP_STORE_ATTR: case OP_BUILD_LIST:
        case OP_BUILD_MAP: case OP_BUILD_TUPLE: case OP_UNPACK_SEQUENCE:
        case OP_LOAD_GLOBAL: case OP_STORE_GLOBAL: case OP_BUILD_SLICE:
        case OP_FOR_ITER: case OP_LIST_APPEND: case OP_MAP_ADD:
        case OP_SET_ADD: case OP_DICT_UPDATE: case OP_LIST_EXTEND:
        case OP_SET_UPDATE: case OP_BUILD_SET: case OP_BUILD_STRING:
        case OP_LOAD_DEREF: case OP_STORE_DEREF: case OP_SETUP_FINALLY:
        case OP_SETUP_WITH: case OP_SETUP_ASYNC_WITH: case OP_RERAISE:
        case OP_GEN_START: case OP_FORMAT_VALUE: case OP_EXTENDED_ARG:
        case OP_MATCH_MAPPING: case OP_MATCH_SEQUENCE: case OP_RAISE_VARARGS:
        case OP_LOAD_FAST: case OP_STORE_FAST: case OP_DELETE_NAME:
        case OP_DELETE_ATTR: case OP_DELETE_SUBSCR: case OP_DELETE_GLOBAL:
        case OP_DELETE_FAST: case OP_UNPACK_EX: case OP_GET_LEN:
            return PROTO_TRUE;
        default:
            return PROTO_FALSE;
    }
}

static const proto::ProtoObject* py_stack_effect(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    // Stub: return 0 for now as most analysis doesn't strictly need it yet
    return ctx->fromInteger(0);
}

// Dummy stubs to satisfy opcode.py
static const proto::ProtoObject* py_null_list_stub(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    return ctx->newList()->asObject(ctx);
}

static const proto::ProtoObject* py_false_stub(proto::ProtoContext*, const proto::ProtoObject*, const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) { return PROTO_FALSE; }

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    PythonEnvironment* env = PythonEnvironment::get(ctx);
    const proto::ProtoObject* mod = ctx->newObject(false);
    if (env && env->getObjectPrototype()) mod = mod->addParent(ctx, env->getObjectPrototype());
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__name__"), PythonEnvironment::getInternedString(ctx, "_opcode")->asObject(ctx));

    std::map<std::string, int> opmap_data = {
        {"LOAD_CONST", OP_LOAD_CONST}, {"RETURN_VALUE", OP_RETURN_VALUE},
        {"LOAD_NAME", OP_LOAD_NAME}, {"STORE_NAME", OP_STORE_NAME},
        {"BINARY_ADD", OP_BINARY_ADD}, {"BINARY_SUBTRACT", OP_BINARY_SUBTRACT},
        {"CALL_FUNCTION", OP_CALL_FUNCTION}, {"BINARY_MULTIPLY", OP_BINARY_MULTIPLY},
        {"BINARY_TRUE_DIVIDE", OP_BINARY_TRUE_DIVIDE}, {"COMPARE_OP", OP_COMPARE_OP},
        {"POP_JUMP_IF_FALSE", OP_POP_JUMP_IF_FALSE}, {"JUMP_ABSOLUTE", OP_JUMP_ABSOLUTE},
        {"LOAD_ATTR", OP_LOAD_ATTR}, {"STORE_ATTR", OP_STORE_ATTR},
        {"BUILD_LIST", OP_BUILD_LIST}, {"BINARY_SUBSCR", OP_BINARY_SUBSCR},
        {"BUILD_MAP", OP_BUILD_MAP}, {"STORE_SUBSCR", OP_STORE_SUBSCR},
        {"BUILD_TUPLE", OP_BUILD_TUPLE}, {"GET_ITER", OP_GET_ITER},
        {"FOR_ITER", OP_FOR_ITER}, {"UNPACK_SEQUENCE", OP_UNPACK_SEQUENCE},
        {"LOAD_GLOBAL", OP_LOAD_GLOBAL}, {"STORE_GLOBAL", OP_STORE_GLOBAL},
        {"BUILD_SLICE", OP_BUILD_SLICE}, {"ROT_TWO", OP_ROT_TWO},
        {"DUP_TOP", OP_DUP_TOP}, {"BINARY_MODULO", OP_BINARY_MODULO},
        {"BINARY_POWER", OP_BINARY_POWER}, {"BINARY_FLOOR_DIVIDE", OP_BINARY_FLOOR_DIVIDE},
        {"UNARY_NEGATIVE", OP_UNARY_NEGATIVE}, {"UNARY_NOT", OP_UNARY_NOT},
        {"UNARY_INVERT", OP_UNARY_INVERT}, {"POP_TOP", OP_POP_TOP},
        {"UNARY_POSITIVE", OP_UNARY_POSITIVE}, {"NOP", OP_NOP},
        {"INPLACE_ADD", OP_INPLACE_ADD}, {"BINARY_LSHIFT", OP_BINARY_LSHIFT},
        {"BINARY_RSHIFT", OP_BINARY_RSHIFT}, {"INPLACE_SUBTRACT", OP_INPLACE_SUBTRACT},
        {"BINARY_AND", OP_BINARY_AND}, {"BINARY_OR", OP_BINARY_OR},
        {"BINARY_XOR", OP_BINARY_XOR}, {"INPLACE_MULTIPLY", OP_INPLACE_MULTIPLY},
        {"INPLACE_TRUE_DIVIDE", OP_INPLACE_TRUE_DIVIDE}, {"INPLACE_FLOOR_DIVIDE", OP_INPLACE_FLOOR_DIVIDE},
        {"INPLACE_MODULO", OP_INPLACE_MODULO}, {"INPLACE_POWER", OP_INPLACE_POWER},
        {"INPLACE_LSHIFT", OP_INPLACE_LSHIFT}, {"INPLACE_RSHIFT", OP_INPLACE_RSHIFT},
        {"INPLACE_AND", OP_INPLACE_AND}, {"INPLACE_OR", OP_INPLACE_OR},
        {"INPLACE_XOR", OP_INPLACE_XOR}, {"ROT_THREE", OP_ROT_THREE},
        {"ROT_FOUR", OP_ROT_FOUR}, {"DUP_TOP_TWO", OP_DUP_TOP_TWO},
        {"BUILD_FUNCTION", OP_BUILD_FUNCTION}, {"LOAD_FAST", OP_LOAD_FAST},
        {"STORE_FAST", OP_STORE_FAST}, {"CALL_FUNCTION_KW", OP_CALL_FUNCTION_KW},
        {"BUILD_CLASS", OP_BUILD_CLASS}, {"DELETE_NAME", OP_DELETE_NAME},
        {"DELETE_ATTR", OP_DELETE_ATTR}, {"DELETE_SUBSCR", OP_DELETE_SUBSCR},
        {"DELETE_GLOBAL", OP_DELETE_GLOBAL}, {"DELETE_FAST", OP_DELETE_FAST},
        {"RAISE_VARARGS", OP_RAISE_VARARGS}, {"UNPACK_EX", OP_UNPACK_EX},
        {"POP_JUMP_IF_TRUE", OP_POP_JUMP_IF_TRUE}, {"LIST_APPEND", OP_LIST_APPEND},
        {"MAP_ADD", OP_MAP_ADD}, {"SET_ADD", OP_SET_ADD},
        {"BUILD_SET", OP_BUILD_SET}, {"YIELD_VALUE", OP_YIELD_VALUE},
        {"SETUP_WITH", OP_SETUP_WITH}, {"WITH_CLEANUP", OP_WITH_CLEANUP},
        {"GET_YIELD_FROM_ITER", OP_GET_YIELD_FROM_ITER}, {"YIELD_FROM", OP_YIELD_FROM},
        {"SETUP_FINALLY", OP_SETUP_FINALLY}, {"POP_BLOCK", OP_POP_BLOCK},
        {"BUILD_STRING", OP_BUILD_STRING}, {"LOAD_DEREF", OP_LOAD_DEREF},
        {"STORE_DEREF", OP_STORE_DEREF}, {"CALL_FUNCTION_EX", OP_CALL_FUNCTION_EX},
        {"LIST_EXTEND", OP_LIST_EXTEND}, {"DICT_UPDATE", OP_DICT_UPDATE},
        {"SET_UPDATE", OP_SET_UPDATE}, {"LIST_TO_TUPLE", OP_LIST_TO_TUPLE},
        {"GET_AWAITABLE", OP_GET_AWAITABLE}, {"GET_AITER", OP_GET_AITER},
        {"GET_ANEXT", OP_GET_ANEXT}, {"EXCEPTION_MATCH", OP_EXCEPTION_MATCH},
        {"SETUP_ASYNC_WITH", OP_SETUP_ASYNC_WITH}, {"BINARY_MATRIX_MULTIPLY", OP_BINARY_MATRIX_MULTIPLY},
        {"INPLACE_MATRIX_MULTIPLY", OP_INPLACE_MATRIX_MULTIPLY}, {"RERAISE", OP_RERAISE},
        {"JUMP_FORWARD", OP_JUMP_FORWARD}, {"FORMAT_VALUE", OP_FORMAT_VALUE},
        {"GEN_START", OP_GEN_START}, {"GET_LEN", OP_GET_LEN},
        {"MATCH_MAPPING", OP_MATCH_MAPPING}, {"MATCH_SEQUENCE", OP_MATCH_SEQUENCE},
        {"EXTENDED_ARG", OP_EXTENDED_ARG}, {"POP_EXCEPT", OP_POP_EXCEPT},
        {"IMPORT_STAR", OP_IMPORT_STAR}, {"IMPORT_FROM", OP_IMPORT_FROM}
    };

    const proto::ProtoObject* opmap = nullptr;
    const proto::ProtoObject* dictClass = env ? env->resolve("dict", ctx) : nullptr;
    if (env && dictClass && dictClass != PROTO_NONE) {
        opmap = env->callObject(dictClass, {});
    } else {
        opmap = ctx->newObject(false);
        if (env && env->getDictPrototype()) opmap = opmap->addParent(ctx, env->getDictPrototype());
        if (env) env->initDictStorage(ctx, opmap);
    }
    
    int maxOp = 255;
    for (const auto& pair : opmap_data) {
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
