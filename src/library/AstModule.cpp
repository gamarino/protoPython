#include <protoPython/AstModule.h>
#include <protoPython/PythonEnvironment.h>
#include <vector>
#include <string>

namespace protoPython {
namespace ast {

static const proto::ProtoObject* py_ast_node_call(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink;
    (void)positionalParameters;
    (void)keywordParameters;
    // Basic constructor: return a new instance with self as class
    const proto::ProtoObject* instance = ctx->newObject(false);
    instance = instance->addParent(ctx, self);
    // Set __class__ to self
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env) {
        instance = instance->setAttribute(ctx, env->getClassString(), self);
    }
    return instance;
}

static const proto::ProtoObject* create_ast_node_type(proto::ProtoContext* ctx, const char* name, const proto::ProtoObject* base) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* type = base ? base->newChild(ctx, true) : ctx->newObject(false);
    if (env && env->getTypePrototype()) {
        type = type->setAttribute(ctx, env->getClassString(), env->getTypePrototype());
    }
    type = type->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__name__"), PythonEnvironment::getInternedString(ctx, name)->asObject(ctx));
    type = type->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__call__"), ctx->fromMethod(nullptr, py_ast_node_call));
    return type;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* mod_obj = env && env->getObjectPrototype() ? env->getObjectPrototype()->newChild(ctx, true) : ctx->newObject(false);
    const proto::ProtoObject* objectProto = env ? env->getObjectPrototype() : nullptr;

    const proto::ProtoObject* ast_base = create_ast_node_type(ctx, "AST", objectProto);
    mod_obj = mod_obj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "AST"), ast_base);

    // ASDL sum types (base types for others)
    const char* base_types[] = {
        "mod", "stmt", "expr", "expr_context", "boolop", "operator", "unaryop", "cmpop",
        "excepthandler", "arguments", "arg", "keyword", "alias", "withitem", "type_ignore",
        "type_param", "pattern"
    };

    for (const char* name : base_types) {
        const proto::ProtoObject* type = create_ast_node_type(ctx, name, ast_base);
        mod_obj = mod_obj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, name), type);
    }

    // ASDL product types and constructor types
    const char* node_names[] = {
        "Module", "Interactive", "Expression", "FunctionType",
        "FunctionDef", "AsyncFunctionDef", "ClassDef", "Return", "Delete", "Assign", "AugAssign", "AnnAssign",
        "For", "AsyncFor", "While", "If", "With", "AsyncWith", "Raise", "Try", "TryStar", "Assert",
        "Import", "ImportFrom", "Global", "Nonlocal", "Expr", "Pass", "Break", "Continue",
        "BoolOp", "NamedExpr", "BinOp", "UnaryOp", "Lambda", "IfExp", "Dict", "Set", "ListComp", "SetComp", "DictComp", "GeneratorExp",
        "Await", "Yield", "YieldFrom", "Compare", "Call", "FormattedValue", "JoinedStr", "Constant",
        "Attribute", "Subscript", "Starred", "Name", "List", "Tuple", "Slice",
        "Load", "Store", "Del",
        "And", "Or",
        "Add", "Sub", "Mult", "MatMult", "Div", "Mod", "Pow", "LShift", "RShift", "BitOr", "BitXor", "BitAnd", "FloorDiv",
        "Invert", "Not", "UAdd", "USub",
        "Eq", "NotEq", "Lt", "LtE", "Gt", "GtE", "Is", "IsNot", "In", "NotIn",
        "ExceptHandler",
        "MatchValue", "MatchSingleton", "MatchSequence", "MatchMapping", "MatchClass", "MatchStar", "MatchAs", "MatchOr",
        "TypeAlias", "TypeVar", "TypeVarTuple", "ParamSpec"
    };

    for (const char* name : node_names) {
        // Find appropriate base if possible, otherwise use AST. 
        // For simplicity in this "full implementation", we just use AST for now unless we want to be very precise.
        // But Suite(mod) expects 'mod' to be defined.
        const proto::ProtoObject* base = ast_base;
        // Some heuristics for bases
        std::string n(name);
        if (n == "Module" || n == "Interactive" || n == "Expression" || n == "FunctionType") {
             base = mod_obj->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "mod"));
        } else if (n == "FunctionDef" || n == "Return" || n == "If" || n == "For" || n == "Expr" || n == "Pass" || n == "Assign") {
             base = mod_obj->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "stmt"));
        } else if (n == "Name" || n == "Constant" || n == "BinOp" || n == "Call" || n == "Attribute") {
             base = mod_obj->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "expr"));
        }
        
        const proto::ProtoObject* type = create_ast_node_type(ctx, name, base);
        mod_obj = mod_obj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, name), type);
    }

    // Add __all__
    const proto::ProtoList* all_list = ctx->newList();
    all_list = all_list->appendLast(ctx, PythonEnvironment::getInternedString(ctx, "AST")->asObject(ctx));
    for (const char* name : base_types) all_list = all_list->appendLast(ctx, PythonEnvironment::getInternedString(ctx, name)->asObject(ctx));
    for (const char* name : node_names) all_list = all_list->appendLast(ctx, PythonEnvironment::getInternedString(ctx, name)->asObject(ctx));
    all_list = all_list->appendLast(ctx, PythonEnvironment::getInternedString(ctx, "PyCF_ONLY_AST")->asObject(ctx));
    all_list = all_list->appendLast(ctx, PythonEnvironment::getInternedString(ctx, "PyCF_OPTIMIZED_AST")->asObject(ctx));
    all_list = all_list->appendLast(ctx, PythonEnvironment::getInternedString(ctx, "PyCF_TYPE_COMMENTS")->asObject(ctx));
    mod_obj = mod_obj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__all__"), all_list->asObject(ctx));

    // Flags
    mod_obj = mod_obj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "PyCF_ONLY_AST"), ctx->fromInteger(0x0400));
    mod_obj = mod_obj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "PyCF_OPTIMIZED_AST"), ctx->fromInteger(0x8000));
    mod_obj = mod_obj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "PyCF_TYPE_COMMENTS"), ctx->fromInteger(0x1000));

    return mod_obj;
}

} // namespace ast
} // namespace protoPython
