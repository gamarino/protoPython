#include <protoPython/Compiler.h>
#include <protoPython/DiagUtils.h>
#include <protoPython/ExecutionEngine.h>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>
#include <iostream>

namespace protoPython {

static void collectGlobalsFromNode(ASTNode* node, std::unordered_set<std::string>& globalsOut);
static void collectUsedNames(ASTNode* node, std::unordered_set<std::string>& out);
static void collectDefinedNames(ASTNode* node, std::unordered_set<std::string>& out);
static void collectNonlocalsFromNode(ASTNode* node, std::unordered_set<std::string>& out);
static void collectCapturedNamesImpl(ASTNode* node, const std::unordered_set<std::string>& globalsInScope, std::unordered_set<std::string>& capturedOut, int depth = 0);
// Buffer to reserve on the VM stack for GC-visible operand storage.
// Must be large enough to hold the maximum operand stack depth needed by any
// single bytecode sequence. A 512-entry dict literal requires 1024 LOAD_CONST
// pushes before BUILD_MAP, plus a few extra for intermediates — so 8192 gives
// comfortable headroom for up to ~4000-entry dict/set/list literals.
constexpr int PYTHON_STACK_BUFFER = 4096;

Compiler::Compiler(proto::ProtoContext* ctx, const std::string& filename)
    : ctx_(ctx), filename_(filename) {
    constantsVec_ = ctx_->newList();
    namesVec_ = ctx_->newList();
    bytecodeVec_ = ctx_->newList();
}

// Returns the Python type-name (e.g. "int", "str", "tuple") if the AST node
// is a non-singleton literal that warrants a SyntaxWarning when used as an
// operand of `is` / `is not`.  CPython's compile() emits the warning for
// int/float/complex/str/bytes/tuple literals, but NOT for the four
// singletons None / True / False / Ellipsis (since `x is None` is the
// idiomatic identity check).  Returns nullptr when no warning should fire.
static const char* literalTypeNameForIsWarning(ASTNode* n) {
    if (!n) return nullptr;
    if (auto* c = dynamic_cast<ConstantNode*>(n)) {
        switch (c->constType) {
            case ConstantNode::ConstType::Int:    return "int";
            case ConstantNode::ConstType::Float:  return "float";
            case ConstantNode::ConstType::Str:    return "str";
            case ConstantNode::ConstType::Bytes:  return "bytes";
            // None, Bool, Ellipsis: idiomatic with `is`, no warning.
            default: return nullptr;
        }
    }
    if (dynamic_cast<TupleLiteralNode*>(n)) return "tuple";
    if (dynamic_cast<ListLiteralNode*>(n)) return "list";
    if (dynamic_cast<SetLiteralNode*>(n)) return "set";
    if (dynamic_cast<DictLiteralNode*>(n)) return "dict";
    return nullptr;
}

int Compiler::addConstant(const proto::ProtoObject* obj) {
    if (obj == PROTO_NONE || obj == PROTO_TRUE || obj == PROTO_FALSE) {
        int n = static_cast<int>(constantsVec_->getSize(ctx_));
        for (int i = 0; i < n; ++i) {
            if (constantsVec_->getAt(ctx_, i) == obj) return i;
        }
        constantsVec_ = constantsVec_->appendLast(ctx_, obj);
        return n;
    }

    if (obj->isInteger(ctx_)) {
        // Use asLong for SmallInt-sized values; fall back to non-deduping
        // push when the integer is a LargeInteger/bignum that asLong
        // would overflow.  Bignum constants are cached by pointer
        // identity instead via constantsVec_ scan.
        try {
            long long val = obj->asLong(ctx_);
            auto it = constIntIndex_.find(val);
            if (it != constIntIndex_.end()) return it->second;
            int idx = static_cast<int>(constantsVec_->getSize(ctx_));
            constantsVec_ = constantsVec_->appendLast(ctx_, obj);
            constIntIndex_[val] = idx;
            return idx;
        } catch (const std::overflow_error&) {
            // Bignum — no numeric cache.
            int n = static_cast<int>(constantsVec_->getSize(ctx_));
            for (int i = 0; i < n; ++i) {
                if (constantsVec_->getAt(ctx_, i) == obj) return i;
            }
            constantsVec_ = constantsVec_->appendLast(ctx_, obj);
            return n;
        }
    }
    if (obj->isDouble(ctx_)) {
        double val = obj->asDouble(ctx_);
        auto it = constFloatIndex_.find(val);
        if (it != constFloatIndex_.end()) return it->second;
        int idx = static_cast<int>(constantsVec_->getSize(ctx_));
        constantsVec_ = constantsVec_->appendLast(ctx_, obj);
        constFloatIndex_[val] = idx;
        return idx;
    }
    if (obj->isString(ctx_)) {
        std::string val;
        obj->asString(ctx_)->toUTF8String(ctx_, val);
        auto it = constStrIndex_.find(val);
        if (it != constStrIndex_.end()) return it->second;
        int idx = static_cast<int>(constantsVec_->getSize(ctx_));
        constantsVec_ = constantsVec_->appendLast(ctx_, obj);
        constStrIndex_[val] = idx;
        return idx;
    }

    // Fallback for other objects (should be rare for constants)
    int n = static_cast<int>(constantsVec_->getSize(ctx_));
    for (int i = 0; i < n; ++i) {
        if (constantsVec_->getAt(ctx_, i) == obj) return i;
    }
    int idx = n;
    constantsVec_ = constantsVec_->appendLast(ctx_, obj);
    return idx;
}

int Compiler::addName(const std::string& name) {
    auto it = namesIndex_.find(name);
    if (it != namesIndex_.end()) return it->second;
    int idx = static_cast<int>(namesVec_->getSize(ctx_));
    auto* env = protoPython::PythonEnvironment::get(ctx_);
    const proto::ProtoObject* str = env ? reinterpret_cast<const proto::ProtoObject*>(env->getInternedString(ctx_, name.c_str())) : proto::ProtoString::createSymbol(ctx_, name.c_str())->asObject(ctx_);
    namesVec_ = namesVec_->appendLast(ctx_, str);
    namesIndex_[name] = idx;
    if (get_env_diag()) {
        fprintf(stderr, "DEBUG COMPILER [%s]: addName '%s' -> idx %d (strObj=%p)\n", filename_.c_str(), name.c_str(), idx, (void*)str);
    }
    return idx;
}

// Returns the net stack effect of opcode `op` with `arg`.
// Positive = pushes items, negative = pops items.
// Conservative: unknown opcodes return 0 (stack level stays unchanged in tracker,
// which overestimates depth when ops pop — safe for slot-array sizing).
static int stackEffect(int op, int arg) {
    switch (op) {
        // Loads (+1)
        case OP_LOAD_CONST: case OP_LOAD_NAME: case OP_LOAD_GLOBAL:
        case OP_LOAD_FAST:  case OP_LOAD_DEREF:
        case OP_DUP_TOP:    case OP_PUSH_NULL:
        case OP_BUILD_RAW_LIST:
            return 1;

        // OP_WRAP_RAW_LIST: pop raw, push wrapped → 0 net.
        // OP_BUILD_ANNOTATE: same shape (replaces TOS).
        case OP_WRAP_RAW_LIST:
        case OP_BUILD_ANNOTATE:
            return 0;
        case OP_DUP_TOP_TWO:
            return 2;

        // Stores (-1)
        case OP_STORE_NAME: case OP_STORE_GLOBAL:
        case OP_STORE_FAST: case OP_STORE_DEREF:
        case OP_POP_TOP:    case OP_RETURN_VALUE:
        case OP_IMPORT_STAR:
            return -1;
        case OP_STORE_ATTR:  // pops value + object
            return -2;
        case OP_STORE_SUBSCR: // pops value + key + container
            return -3;

        // Unary ops (0 net: pop 1, push 1)
        case OP_UNARY_NEGATIVE: case OP_UNARY_POSITIVE:
        case OP_UNARY_NOT:      case OP_UNARY_INVERT:
        case OP_GET_ITER:       case OP_LIST_TO_TUPLE:
        case OP_GET_LEN:        // actually +1 (pushes len w/o popping), count as 0 → safe
        case OP_BUILD_FUNCTION:
        case OP_GET_AWAITABLE:  case OP_GET_AITER:
        case OP_GET_YIELD_FROM_ITER:
            return 0;

        // PD1: GET_ANEXT keeps the aiter on the stack and pushes the
        // awaitable on top — net +1.  Was previously falling through
        // to the default 0, causing per-iteration stack-budget
        // underestimation in `async for` loops, which overflowed the
        // GC-rooted stack on the second send().
        case OP_GET_ANEXT:
            return 1;

        // LOAD_ATTR: pops object, but when loading a method replaces it with NULL and
        // pushes the method (+1 net). Accounting for the worst case (method) avoids
        // GCStack overflow from systematic underestimation of max stack depth.
        case OP_LOAD_ATTR:
            return 1;

        // Binary ops (-1 net: pop 2, push 1)
        case OP_BINARY_ADD:       case OP_BINARY_SUBTRACT:
        case OP_BINARY_MULTIPLY:  case OP_BINARY_TRUE_DIVIDE:
        case OP_BINARY_FLOOR_DIVIDE: case OP_BINARY_MODULO:
        case OP_BINARY_POWER:     case OP_BINARY_LSHIFT:
        case OP_BINARY_RSHIFT:    case OP_BINARY_AND:
        case OP_BINARY_OR:        case OP_BINARY_XOR:
        case OP_BINARY_MATRIX_MULTIPLY:
        case OP_INPLACE_ADD:      case OP_INPLACE_SUBTRACT:
        case OP_INPLACE_MULTIPLY: case OP_INPLACE_TRUE_DIVIDE:
        case OP_INPLACE_FLOOR_DIVIDE: case OP_INPLACE_MODULO:
        case OP_INPLACE_POWER:    case OP_INPLACE_LSHIFT:
        case OP_INPLACE_RSHIFT:   case OP_INPLACE_AND:
        case OP_INPLACE_OR:       case OP_INPLACE_XOR:
        case OP_INPLACE_MATRIX_MULTIPLY:
        case OP_COMPARE_OP:       case OP_BINARY_SUBSCR:
        case OP_EXCEPTION_MATCH:
            return -1;

        // Jumps (pops condition)
        case OP_POP_JUMP_IF_FALSE: case OP_POP_JUMP_IF_TRUE:
            return -1;
        case OP_JUMP_ABSOLUTE: case OP_JUMP_FORWARD: case OP_NOP:
        case OP_ROT_TWO:       case OP_ROT_THREE:    case OP_ROT_FOUR:
        case OP_EXTENDED_ARG:
            return 0;

        // FOR_ITER: when the iterator has items, pushes next value (+1).
        // When done, falls through with the iterator still on stack (0 relative).
        // Use +1 for sizing (the pushed case drives maxStack).
        case OP_FOR_ITER:
            return 1;

        // Builders: pop `arg` items, push 1
        case OP_BUILD_LIST: case OP_BUILD_TUPLE: case OP_BUILD_SET:
        case OP_BUILD_STRING:
            return 1 - arg;
        case OP_BUILD_MAP:
            return 1 - 2 * arg;
        case OP_BUILD_SLICE:
            return 1 - arg; // arg is 2 or 3

        // CALL_FUNCTION: modern layout [NULL, callable, a1..aN] → pops N+2, pushes 1
        case OP_CALL_FUNCTION:
            return -(arg + 1);
        case OP_CALL_FUNCTION_KW:
            return -(arg + 2); // also pops kwnames tuple
        case OP_CALL_FUNCTION_EX:
            return -(1 + (arg & 1)); // EX pops args-tuple [+ kwargs-dict]

        // Comprehension helpers: pop TOS into container at stack[top-arg-1], no push
        case OP_LIST_APPEND: case OP_SET_ADD:
            return -1;
        case OP_MAP_ADD:
            return -2;

        // IMPORT_FROM: keeps module on stack and pushes the imported attribute (+1 net)
        case OP_IMPORT_FROM:
            return 1;

        // Sequence helpers
        case OP_LIST_EXTEND: case OP_DICT_UPDATE: case OP_SET_UPDATE:
            return -1;

        // UNPACK_SEQUENCE: pop 1, push arg items
        case OP_UNPACK_SEQUENCE:
            return arg - 1;

        // Yield: yields value and receives sent value; net 0
        case OP_YIELD_VALUE: case OP_YIELD_FROM:
        case OP_GEN_START:
            return 0;

        // Exception/cleanup
        case OP_POP_EXCEPT:
        case OP_WITH_CLEANUP:
            return -1;
        case OP_POP_BLOCK: case OP_SETUP_FINALLY:
        case OP_RERAISE:
            return 0;
        // SETUP_WITH pops the context manager and pushes __exit__ + __enter__() result (+1 net)
        case OP_SETUP_WITH: case OP_SETUP_ASYNC_WITH:
            return 1;

        case OP_RAISE_VARARGS:
            return -arg;

        default:
            return 0;
    }
}

void Compiler::emit(int op, int arg) {
    if (get_env_diag()) {
        fprintf(stderr, "COMPILING [%p]: offset=%zu op=%d arg=%d\n", (void*)this, static_cast<size_t>(bytecodeVec_->getSize(ctx_)), op, arg);
    }
    bytecodeVec_ = bytecodeVec_->appendLast(ctx_, ctx_->fromInteger(op));
    bytecodeVec_ = bytecodeVec_->appendLast(ctx_, ctx_->fromInteger(arg));
    currentStack_ += stackEffect(op, arg);
    if (currentStack_ > maxStack_) maxStack_ = currentStack_;
}

int Compiler::bytecodeOffset() const {
    return static_cast<int>(bytecodeVec_->getSize(ctx_)) / 2;
}

void Compiler::addPatch(int argSlotIndex, int targetBytecodeIndex) {
    patches_.emplace_back(argSlotIndex, targetBytecodeIndex);
}

void Compiler::applyPatches() {
    for (const auto& p : patches_) {
        // p.first is the instruction index (bytecodeOffset() value)
        // the arg slot is at index (p.first * 2) + 1 in the bytecodeVec_
        unsigned long arrayIdx = static_cast<unsigned long>(p.first) * 2 + 1;
        if (arrayIdx < static_cast<unsigned long>(bytecodeVec_->getSize(ctx_)))
            bytecodeVec_ = bytecodeVec_->setAt(ctx_, arrayIdx, ctx_->fromInteger(p.second * 2)); // ExecutionEngine jumps to array index!
    }
    patches_.clear();
}

const proto::ProtoTuple* Compiler::getConstants() {
    if (!constants_) {
        constants_ = ctx_->newTupleFromList(constantsVec_);
    }
    return constants_;
}

const proto::ProtoTuple* Compiler::getNames() {
    if (!names_) {
        names_ = ctx_->newTupleFromList(namesVec_);
    }
    return names_;
}

const proto::ProtoTuple* Compiler::getBytecode() {
    if (!bytecode_) {
        bytecode_ = ctx_->newTupleFromList(bytecodeVec_);
    }
    return bytecode_;
}

bool Compiler::compileConstant(ConstantNode* n) {
    if (!n) return false;
    // Emit the lexer-attached SyntaxWarning, if any, before producing
    // bytecode for this constant.  Used for "invalid <kind> literal"
    // when a numeric token is immediately followed by a Python keyword
    // (e.g. `9and x` → "invalid decimal literal").  When `simplefilter
    // ('error', SyntaxWarning)` is active the warn becomes a SyntaxError
    // and emitSyntaxWarning returns true to halt compilation.
    if (!n->pendingWarning.empty()) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx_);
        if (env && env->emitSyntaxWarning(ctx_, n->pendingWarning, filename_, n->line)) {
            return false;
        }
    }
    const proto::ProtoObject* obj = nullptr;
    if (n->constType == ConstantNode::ConstType::Int) {
        if (!n->bigIntDigits.empty()) {
            // Integer literal overflows int64; build a bignum from the
            // raw digits via ProtoContext::fromString(digits, base).
            obj = ctx_->fromString(n->bigIntDigits.c_str(), n->bigBase);
            if (!obj) obj = ctx_->fromInteger(0);
        } else {
            obj = ctx_->fromInteger(n->intVal);
        }
    }
    else if (n->constType == ConstantNode::ConstType::Float)
        obj = ctx_->fromDouble(n->floatVal);
    else if (n->constType == ConstantNode::ConstType::Imaginary) {
        // Build a complex(0, n->floatVal) constant by allocating a
        // fresh complex instance with real=0 and imag=floatVal.  The
        // complex prototype supplies __eq__/__add__/etc.
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx_);
        if (env && env->getComplexPrototype()) {
            proto::ProtoObject* c = const_cast<proto::ProtoObject*>(env->getComplexPrototype()->newChild(ctx_, true));
            c->setAttribute(ctx_, PythonEnvironment::getInternedString(ctx_, "real"), ctx_->fromDouble(0.0));
            c->setAttribute(ctx_, PythonEnvironment::getInternedString(ctx_, "imag"), ctx_->fromDouble(n->floatVal));
            c->setAttribute(ctx_, PythonEnvironment::getInternedString(ctx_, "__class__"), env->getComplexPrototype());
            obj = c;
        } else {
            obj = ctx_->fromDouble(n->floatVal);
        }
    }
    else if (n->constType == ConstantNode::ConstType::Str) {
        if (n->strVal.find('\0') != std::string::npos) {
            // Embedded NUL: getInternedString's strlen would truncate
            // the literal at the first 0x00.  Build from an explicit
            // byte length instead (interning is skipped for this rare
            // case — correctness over the dedup optimisation).
            uint8_t rem[4]; uint8_t remCount = 0;
            const proto::ProtoString* res = proto::ProtoString::fromUTF8Buffer(
                ctx_, reinterpret_cast<const uint8_t*>(n->strVal.data()),
                n->strVal.size(), nullptr, 0, rem, &remCount);
            obj = res ? res->asObject(ctx_) : PROTO_NONE;
        } else {
            obj = PythonEnvironment::getInternedString(ctx_, n->strVal.c_str())->asObject(ctx_);
        }
    }
    else if (n->constType == ConstantNode::ConstType::Bytes) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx_);
        if (env && env->getBytesPrototype()) {
            proto::ProtoObject* b = const_cast<proto::ProtoObject*>(env->getBytesPrototype()->newChild(ctx_, true));
            // Store raw octets in a ProtoByteBuffer (preserves embedded
            // 0x00 and bytes >= 0x80 unchanged).  bytesVal is a
            // std::string used as a raw-bytes buffer.
            const proto::ProtoByteBuffer* bb = ctx_->newByteBuffer(
                n->bytesVal.data(),
                static_cast<unsigned long>(n->bytesVal.size()));
            b->setAttribute(ctx_,
                PythonEnvironment::getInternedString(ctx_, "__data__"),
                bb->asObject(ctx_));
            // Set __class__ so getType() returns bytesPrototype (not typePrototype via prototype-chain lookup).
            b->setAttribute(ctx_, PythonEnvironment::getInternedString(ctx_, "__class__"), env->getBytesPrototype());
            obj = b;
        } else {
            obj = PythonEnvironment::getInternedString(ctx_, n->bytesVal.c_str())->asObject(ctx_);
        }
    }
    else if (n->constType == ConstantNode::ConstType::None)
        obj = PROTO_NONE;
    else if (n->constType == ConstantNode::ConstType::Bool)
        obj = n->intVal ? PROTO_TRUE : PROTO_FALSE;
    else if (n->constType == ConstantNode::ConstType::Ellipsis) {
        PythonEnvironment* env = PythonEnvironment::get(ctx_);
        obj = env ? env->getEllipsisPrototype() : PythonEnvironment::getInternedString(ctx_, "...")->asObject(ctx_);
    }
    if (!obj && n->constType != ConstantNode::ConstType::None) return false;
    int idx = addConstant(obj);
    emit(OP_LOAD_CONST, idx);
    return true;
}

bool Compiler::compileName(NameNode* n, bool pushNull) {
    if (!n) return false;
    return emitNameOp(n->id, TargetCtx::Load, pushNull);
}

bool Compiler::compileBinOp(BinOpNode* n) {
    if (!n) return false;

    if (n->op == TokenType::And) {
        if (!compileNode(n->left.get())) return false;
        emit(OP_DUP_TOP, 0);
        emit(OP_POP_JUMP_IF_FALSE, 0);
        int jumpIdx = bytecodeOffset() - 1;
        emit(OP_POP_TOP, 0);
        if (!compileNode(n->right.get())) return false;
        addPatch(jumpIdx, bytecodeOffset());
        return true;
    } else if (n->op == TokenType::Or) {
        if (!compileNode(n->left.get())) return false;
        emit(OP_DUP_TOP, 0);
        emit(OP_POP_JUMP_IF_TRUE, 0);
        int jumpIdx = bytecodeOffset() - 1;
        emit(OP_POP_TOP, 0);
        if (!compileNode(n->right.get())) return false;
        addPatch(jumpIdx, bytecodeOffset());
        return true;
    }

    // Detect chained comparisons (e.g., a == b == c, a < b < c).
    // The parser builds left-nested trees: ((a==b)==c) instead of the proper chained form.
    // We flatten the chain and generate correct short-circuit bytecode.
    auto isCompOp = [](TokenType t) {
        return t == TokenType::EqEqual || t == TokenType::NotEqual ||
               t == TokenType::Less || t == TokenType::LessEqual ||
               t == TokenType::Greater || t == TokenType::GreaterEqual ||
               t == TokenType::Is || t == TokenType::IsNot ||
               t == TokenType::In || t == TokenType::NotIn;
    };
    if (isCompOp(n->op)) {
        BinOpNode* leftBin = dynamic_cast<BinOpNode*>(n->left.get());
        if (leftBin && isCompOp(leftBin->op) && !leftBin->parenthesized) {
            // Flatten the left-nested chain into (leftmost, [(op,rhs)...]) order.
            struct CmpEntry { TokenType op; ASTNode* right; };
            std::vector<CmpEntry> chain;
            ASTNode* leftmost = nullptr;
            BinOpNode* cur = n;
            while (cur) {
                chain.push_back({cur->op, cur->right.get()});
                BinOpNode* nxtLeft = dynamic_cast<BinOpNode*>(cur->left.get());
                if (nxtLeft && isCompOp(nxtLeft->op)) {
                    cur = nxtLeft;
                } else {
                    leftmost = cur->left.get();
                    break;
                }
            }
            std::reverse(chain.begin(), chain.end());

            // Emit "is/is not with literal" SyntaxWarning for any chain
            // segment whose op is `is` or `is not` and whose LHS or RHS is
            // a non-singleton literal.  For chain[0] the LHS is `leftmost`;
            // for chain[k>=1] the LHS is the previous comparator (chain[k-1].right).
            for (size_t k = 0; k < chain.size(); ++k) {
                if (chain[k].op != TokenType::Is && chain[k].op != TokenType::IsNot) continue;
                ASTNode* lhs = (k == 0) ? leftmost : chain[k - 1].right;
                const char* litR = literalTypeNameForIsWarning(chain[k].right);
                const char* litL = litR ? nullptr : literalTypeNameForIsWarning(lhs);
                const char* litType = litR ? litR : litL;
                if (!litType) continue;
                const char* opStr = (chain[k].op == TokenType::Is) ? "is" : "is not";
                std::string msg = std::string("\"") + opStr + "\" with '" + litType +
                                  "' literal. Did you mean \"==\"?";
                if (PythonEnvironment* env = PythonEnvironment::fromContext(ctx_)) {
                    int line = n->line > 0 ? n->line : 1;
                    if (env->emitSyntaxWarning(ctx_, msg, filename_, line)) {
                        return false;
                    }
                }
            }

            auto cmpArg = [](TokenType t) -> int {
                switch (t) {
                    case TokenType::EqEqual:      return 0;
                    case TokenType::NotEqual:     return 1;
                    case TokenType::Less:         return 2;
                    case TokenType::LessEqual:    return 3;
                    case TokenType::Greater:      return 4;
                    case TokenType::GreaterEqual: return 5;
                    case TokenType::In:           return 6;
                    case TokenType::NotIn:        return 7;
                    case TokenType::Is:           return 8;
                    case TokenType::IsNot:        return 9;
                    default:                      return 0;
                }
            };

            if (!compileNode(leftmost)) return false;

            std::vector<int> falseJumps;
            for (size_t k = 0; k < chain.size(); k++) {
                bool isLast = (k == chain.size() - 1);
                if (!compileNode(chain[k].right)) return false;
                if (!isLast) {
                    // Preserve rhs for the next comparison via DUP_TOP + ROT_THREE.
                    // Stack before: [..., lhs, rhs]
                    // After DUP_TOP:  [..., lhs, rhs, rhs_dup]
                    // After ROT_THREE: [..., rhs_dup, lhs, rhs]
                    // After COMPARE:  [..., rhs_dup, result]
                    emit(OP_DUP_TOP, 0);
                    emit(OP_ROT_THREE, 0);
                    emit(OP_COMPARE_OP, cmpArg(chain[k].op));
                    emit(OP_POP_JUMP_IF_FALSE, 0);
                    falseJumps.push_back(bytecodeOffset() - 1);
                    // Truthy path: result was popped, stack: [..., rhs_dup]
                } else {
                    // Last comparison: no need to preserve rhs.
                    emit(OP_COMPARE_OP, cmpArg(chain[k].op));
                }
            }
            // Jump over false-path cleanup (truthy end falls through here).
            emit(OP_JUMP_ABSOLUTE, 0);
            int endJumpIdx = bytecodeOffset() - 1;
            // False-path: all intermediate false-jumps land here with one leftover dup.
            int falseTarget = bytecodeOffset();
            for (int idx : falseJumps) {
                addPatch(idx, falseTarget);
            }
            emit(OP_POP_TOP, 0);   // discard leftover dup
            emit(OP_LOAD_CONST, addConstant(PROTO_FALSE));
            // Patch end jump past false-path cleanup.
            addPatch(endJumpIdx, bytecodeOffset());
            return true;
        }
    }

    if (!compileNode(n->left.get()) || !compileNode(n->right.get()))
        return false;
    int op = OP_BINARY_ADD;
    if (n->op == TokenType::Plus) op = OP_BINARY_ADD;
    else if (n->op == TokenType::Minus) op = OP_BINARY_SUBTRACT;
    else if (n->op == TokenType::Star) op = OP_BINARY_MULTIPLY;
    else if (n->op == TokenType::Slash) op = OP_BINARY_TRUE_DIVIDE;
    else if (n->op == TokenType::EqEqual) {
        emit(OP_COMPARE_OP, 0); // 0 is '=='
        return true;
    } else if (n->op == TokenType::Is || n->op == TokenType::IsNot) {
        // CPython emits a SyntaxWarning when one operand of `is` / `is not`
        // is a non-singleton literal — e.g. `x is 1`, `x is "thing"`,
        // `(1, 2) is not x`.  None / True / False / Ellipsis are excluded
        // because `x is None` is the idiomatic identity check.
        const char* litR = literalTypeNameForIsWarning(n->right.get());
        const char* litL = litR ? nullptr : literalTypeNameForIsWarning(n->left.get());
        const char* litType = litR ? litR : litL;
        if (litType) {
            const char* opStr = (n->op == TokenType::Is) ? "is" : "is not";
            std::string msg = std::string("\"") + opStr + "\" with '" + litType +
                              "' literal. Did you mean \"==\"?";
            if (PythonEnvironment* env = PythonEnvironment::fromContext(ctx_)) {
                int line = n->line > 0 ? n->line : 1;
                if (env->emitSyntaxWarning(ctx_, msg, filename_, line)) {
                    // Filter='error' converted the warning to a SyntaxError.
                    return false;
                }
            }
        }
        emit(OP_COMPARE_OP, n->op == TokenType::Is ? 8 : 9);
        return true;
    } else if (n->op == TokenType::In) {
        emit(OP_COMPARE_OP, 6); // 6 is 'in'
        return true;
    } else if (n->op == TokenType::NotIn) {
        emit(OP_COMPARE_OP, 7); // 7 is 'not in'
        return true;
    } else if (n->op == TokenType::NotEqual) {
        emit(OP_COMPARE_OP, 1);
        return true;
    } else if (n->op == TokenType::Less) {
        emit(OP_COMPARE_OP, 2);
        return true;
    } else if (n->op == TokenType::LessEqual) {
        emit(OP_COMPARE_OP, 3);
        return true;
    } else if (n->op == TokenType::Greater) {
        emit(OP_COMPARE_OP, 4);
        return true;
    } else if (n->op == TokenType::GreaterEqual) {
        emit(OP_COMPARE_OP, 5);
        return true;
    } else if (n->op == TokenType::Modulo) {
        op = OP_BINARY_MODULO;
    } else if (n->op == TokenType::DoubleSlash) {
        op = OP_BINARY_FLOOR_DIVIDE;
    } else if (n->op == TokenType::DoubleStar) {
        op = OP_BINARY_POWER;
    } else if (n->op == TokenType::At) {
        op = OP_BINARY_MATRIX_MULTIPLY;
    } else if (n->op == TokenType::LShift) {
        op = OP_BINARY_LSHIFT;
    } else if (n->op == TokenType::RShift) {
        op = OP_BINARY_RSHIFT;
    } else if (n->op == TokenType::BitAnd) {
        op = OP_BINARY_AND;
    } else if (n->op == TokenType::BitOr) {
        op = OP_BINARY_OR;
    } else if (n->op == TokenType::BitXor) {
        op = OP_BINARY_XOR;
    } else {
        // Unknown op
        return false;
    }
    emit(op, 0);
    return true;
}

bool Compiler::compileUnaryOp(UnaryOpNode* n) {
    if (!n || !compileNode(n->operand.get())) return false;
    int op = OP_UNARY_NOT;
    if (n->op == TokenType::Minus) op = OP_UNARY_NEGATIVE;
    else if (n->op == TokenType::Plus) op = OP_UNARY_POSITIVE;
    else if (n->op == TokenType::Tilde) op = OP_UNARY_INVERT;
    else if (n->op == TokenType::Not) op = OP_UNARY_NOT;
    else return false;
    emit(op, 0);
    return true;
}

bool Compiler::compileStarred(StarredNode* n) {
    if (!n) return false;
    return compileNode(n->value.get());
}

bool Compiler::compileCall(CallNode* n) {
    if (!n) return false;

    // Special case: super() with no args inside a class method.
    // Rewrite to super(<defining_class>, self) where the defining class
    // is loaded via emitNameOp (LOAD_DEREF for closure capture, then
    // LOAD_GLOBAL for module scope, then LOAD_NAME).  Multi-level
    // super() across classes defined inside the same function still
    // depends on the class name being resolvable by the inner method —
    // which works for module-level classes but is fragile for
    // classes-in-functions.  A fully correct __class__ cell mechanism
    // (CPython's __classcell__) is deferred to a future round.
    if (isFunctionScope_ && !currentClassName_.empty() && n->args.empty() && n->keywords.empty()) {
        if (auto* nameN = dynamic_cast<NameNode*>(n->func.get())) {
            if (nameN->id == "super") {
                if (!emitNameOp("super", TargetCtx::Load, /*pushNull=*/true)) return false;
                if (!emitNameOp(currentClassName_, TargetCtx::Load)) return false;
                emit(OP_LOAD_FAST, 0);
                emit(OP_CALL_FUNCTION, 2);
                return true;
            }
        }
    }

    // For 3.11+ compatibility, we must ensure there are 2 slots for the callable segment.
    if (auto* attrN = dynamic_cast<AttributeNode*>(n->func.get())) {
        // Attributes handle their own marker (self or NULL)
        if (!compileAttribute(attrN, true)) return false;
    } else if (auto* nameN = dynamic_cast<NameNode*>(n->func.get())) {
        // Names handles their own NULL marker bit
        if (!compileName(nameN, true)) return false;
    } else {
        // Complex expressions (e.g. results of other calls) need an explicit NULL marker.
        emit(OP_PUSH_NULL);
        if (!compileNode(n->func.get())) return false;
    }

    
    bool hasUnpacking = false;
    for (auto& a : n->args) if (dynamic_cast<StarredNode*>(a.get())) { hasUnpacking = true; break; }
    if (!hasUnpacking) {
        for (auto& kw : n->keywords) if (kw.first.empty()) { hasUnpacking = true; break; }
    }

    if (hasUnpacking) {
        // Build positional args tuple
        emit(OP_BUILD_LIST, 0);
        for (auto& a : n->args) {
            if (auto* s = dynamic_cast<StarredNode*>(a.get())) {
                if (!compileNode(s->value.get())) return false;
                emit(OP_LIST_EXTEND, 1);
            } else {
                if (!compileNode(a.get())) return false;
                emit(OP_LIST_APPEND, 1);
            }
        }
        emit(OP_LIST_TO_TUPLE, 0);

        // Build keyword args dict
        bool hasKw = false;
        if (!n->keywords.empty()) {
            emit(OP_BUILD_MAP, 0);
            for (auto& kw : n->keywords) {
                if (kw.first.empty()) {
                    if (!compileNode(kw.second.get())) return false;
                    emit(OP_DICT_UPDATE, 1);
                } else {
                    // OP_MAP_ADD impl reads `key` from TOS and `val` from TOS-1,
                    // and resolves `mapObj` at stack[size - arg - 1]. The dict
                    // is at TOS-2 here (we have [map, val, key] above it after
                    // the loads), so we push value FIRST and key SECOND, then
                    // emit arg=2 so mapObj resolves to `map`.
                    if (!compileNode(kw.second.get())) return false;
                    int nameIdx = addConstant(PythonEnvironment::getInternedString(ctx_, kw.first.c_str())->asObject(ctx_));
                    emit(OP_LOAD_CONST, nameIdx);
                    emit(OP_MAP_ADD, 2);
                }
            }
            hasKw = true;
        }

        emit(OP_CALL_FUNCTION_EX, hasKw ? 1 : 0);
        return true;
    }

    for (auto& arg : n->args) {
        if (!compileNode(arg.get())) return false;
    }

    if (n->keywords.empty()) {
        emit(OP_CALL_FUNCTION, static_cast<int>(n->args.size()));
    } else {
        const proto::ProtoList* kwList = ctx_->newList();
        for (auto& kw : n->keywords) {
            if (!compileNode(kw.second.get())) return false;
            kwList = kwList->appendLast(ctx_, PythonEnvironment::getInternedString(ctx_, kw.first.c_str())->asObject(ctx_));
        }
        const proto::ProtoObject* nameTuple = ctx_->newTupleFromList(kwList)->asObject(ctx_);
        
        int idx = addConstant(nameTuple);
        emit(OP_LOAD_CONST, idx);
        emit(OP_CALL_FUNCTION_KW, static_cast<int>(n->args.size() + n->keywords.size()));
    }
    return true;
}

bool Compiler::compileAttribute(AttributeNode* n, bool pushNull) {
    if (!n || !compileNode(n->value.get())) return false;
    // CPython PEP 8 private-name mangling: in a class body (or a
    // method nested inside one), `obj.__attr` (two-leading-underscores,
    // not double-underscore-suffix) becomes `obj._<ClassName>__attr`.
    int idx = addName(mangleIdentifier(n->attr));
    emit(OP_LOAD_ATTR, (idx << 1) | (pushNull ? 1 : 0));
    return true;
}

bool Compiler::compileSubscript(SubscriptNode* n) {
    if (!n || !compileNode(n->value.get())) return false;
    if (auto* sl = dynamic_cast<SliceNode*>(n->index.get())) {
        if (!compileSlice(sl)) return false;
    } else {
        if (!compileNode(n->index.get())) return false;
    }
    emit(OP_BINARY_SUBSCR, 0);
    return true;
}

bool Compiler::compileSlice(SliceNode* n) {
    if (!n) return false;
    if (n->start) {
        if (!compileNode(n->start.get())) return false;
    } else {
        int idx = addConstant(PROTO_NONE);
        emit(OP_LOAD_CONST, idx);
    }
    if (n->stop) {
        if (!compileNode(n->stop.get())) return false;
    } else {
        int idx = addConstant(PROTO_NONE);
        emit(OP_LOAD_CONST, idx);
    }
    if (n->step) {
        if (!compileNode(n->step.get())) return false;
        emit(OP_BUILD_SLICE, 3);
    } else {
        emit(OP_BUILD_SLICE, 2);
    }
    return true;
}

// Best-effort name of a non-callable / non-subscriptable expression for the
// "perhaps you missed a comma?" SyntaxWarning.  Returns the empty string
// if the node is not one of the literal/comp/lambda/constant kinds we
// recognise.  Only used to format the warning message; the *decision* to
// warn is made by the call sites below.
static std::string missedCommaTypeName(const ASTNode* n) {
    if (!n) return "";
    if (dynamic_cast<const TupleLiteralNode*>(n)) return "tuple";
    if (dynamic_cast<const ListLiteralNode*>(n)) return "list";
    if (dynamic_cast<const ListCompNode*>(n)) return "list";
    if (dynamic_cast<const SetLiteralNode*>(n)) return "set";
    if (dynamic_cast<const SetCompNode*>(n)) return "set";
    if (dynamic_cast<const DictLiteralNode*>(n)) return "dict";
    if (dynamic_cast<const DictCompNode*>(n)) return "dict";
    if (dynamic_cast<const GeneratorExpNode*>(n)) return "generator";
    if (auto* j = dynamic_cast<const JoinedStrNode*>(n)) {
        // PEP 750 distinguishes t"..." (string.templatelib.Template,
        // not subscriptable / not sequence-like) from f"..." (str,
        // sequence-like, indexable).  Report the right runtime type.
        return j->isTString ? "string.templatelib.Template" : "str";
    }
    if (dynamic_cast<const LambdaNode*>(n)) return "function";
    if (auto* c = dynamic_cast<const ConstantNode*>(n)) {
        switch (c->constType) {
            case ConstantNode::ConstType::Int:      return "int";
            case ConstantNode::ConstType::Float:    return "float";
            case ConstantNode::ConstType::Str:      return "str";
            case ConstantNode::ConstType::Bytes:    return "bytes";
            case ConstantNode::ConstType::None:     return "NoneType";
            case ConstantNode::ConstType::Bool:     return "bool";
            case ConstantNode::ConstType::Ellipsis: return "ellipsis";
        }
    }
    return "";
}

// Check a single sequence-literal element for the "missed comma" pattern
// and emit a SyntaxWarning if it matches.  Returns true if compilation
// should abort (the warning was promoted to a SyntaxError under
// filter='error').  CPython produces three flavours of message; we mirror
// them so test_warn_missed_comma's assertRegex matches:
//
//   1. `[<literal>(args)]`             → "<type> is not callable;
//                                          perhaps you missed a comma?"
//   2. `[<unsubscriptable>[i, j]]`     → "<type> is not subscriptable;
//                                          perhaps you missed a comma?"
//   3. `[<sequence>[(i, j)]]`          → "<type> indices must be integers
//                                          or slices, not tuple;
//                                          perhaps you missed a comma?"
bool Compiler::warnIfMissedComma(ASTNode* elem) {
    if (!elem) return false;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx_);
    if (!env) return false;

    if (auto* call = dynamic_cast<CallNode*>(elem)) {
        const ASTNode* func = call->func.get();
        // Lambdas are callable.  Calling them in a list literal is the
        // intended use case (`[(lambda x, y: x) (3, 4)]`) — no warning.
        if (dynamic_cast<const LambdaNode*>(func)) return false;
        std::string ty = missedCommaTypeName(func);
        if (!ty.empty()) {
            std::string msg = "'" + ty + "' object is not callable; perhaps you missed a comma?";
            int line = call->line > 0 ? call->line : 1;
            return env->emitSyntaxWarning(ctx_, msg, filename_, line);
        }
    }
    if (auto* sub = dynamic_cast<SubscriptNode*>(elem)) {
        const ASTNode* base = sub->value.get();
        const ASTNode* index = sub->index.get();
        std::string ty = missedCommaTypeName(base);
        if (ty.empty()) return false;     // base not a recognised literal kind
        // Dict literals accept arbitrary hashable keys including tuples,
        // so `[{(1, 2): 3} [i, j]]` is valid subscription, not a missed
        // comma.  Same for dict comprehensions.
        if (ty == "dict") return false;

        // Categorise base: sequence-like containers (tuple/list/listcomp/
        // str/bytes) accept only integer / slice indices, so an
        // "indices must be integers or slices, not <indexType>" message
        // is correct whenever the index is a recognisable non-integer
        // node.  Everything else (sets, dicts, generators, ...) gets
        // the simpler "is not subscriptable" wording.
        // Sequence-like = supports integer/slice indexing.  f-strings
        // (JoinedStrNode with isTString=false) are str at runtime;
        // t-strings (isTString=true) are string.templatelib.Template
        // and do NOT support subscription, so they fall through to the
        // "is not subscriptable" branch.
        const JoinedStrNode* baseJoined = dynamic_cast<const JoinedStrNode*>(base);
        bool isSequenceLike = dynamic_cast<const TupleLiteralNode*>(base)
                           || dynamic_cast<const ListLiteralNode*>(base)
                           || dynamic_cast<const ListCompNode*>(base)
                           || (baseJoined && !baseJoined->isTString)
                           || (dynamic_cast<const ConstantNode*>(base)
                               && (static_cast<const ConstantNode*>(base)->constType == ConstantNode::ConstType::Str
                                || static_cast<const ConstantNode*>(base)->constType == ConstantNode::ConstType::Bytes));
        // Index classification — only flag types that are unambiguously
        // wrong for a sequence subscript.  Skip:
        //   - Name (identifier): runtime value unknown, may be int.
        //   - Slice: valid.
        //   - Integer / bool literal: valid.
        // Everything else recognised by missedCommaTypeName is wrong.
        if (dynamic_cast<const NameNode*>(index)) return false;
        if (dynamic_cast<const SliceNode*>(index)) return false;
        std::string idxTy = missedCommaTypeName(index);
        if (idxTy == "int" || idxTy == "bool") idxTy.clear();

        std::string msg;
        if (isSequenceLike && !idxTy.empty()) {
            msg = "'" + ty + "' indices must be integers or slices, not "
                + idxTy + "; perhaps you missed a comma?";
        } else if (!isSequenceLike) {
            // Non-sequence base: only warn when index looks tuple-shaped
            // (e.g. `[{1, 2} [i, j]]` — index is a tuple of two names).
            // A bare Name or single literal index could be a legitimate
            // hashable lookup the user is just expressing oddly.
            if (!dynamic_cast<const TupleLiteralNode*>(index)) return false;
            msg = "'" + ty + "' object is not subscriptable; perhaps you missed a comma?";
        } else {
            // Sequence base + index is Tuple/Slice/Name → already handled.
            return false;
        }
        int line = sub->line > 0 ? sub->line : 1;
        return env->emitSyntaxWarning(ctx_, msg, filename_, line);
    }
    return false;
}

bool Compiler::compileListLiteral(ListLiteralNode* n) {
    if (!n) return false;
    // Best-effort missed-comma SyntaxWarning detection (CPython's
    // compile-time warning).  Walk each element for the patterns
    // described in warnIfMissedComma and bail if a warning was
    // promoted to SyntaxError under filter='error'.
    for (auto& e : n->elements) {
        if (warnIfMissedComma(e.get())) return false;
    }
    bool hasStarred = false;
    for (auto& e : n->elements) {
        if (dynamic_cast<StarredNode*>(e.get())) { hasStarred = true; break; }
    }

    if (!hasStarred) {
        for (auto& e : n->elements) {
            if (!compileNode(e.get())) return false;
        }
        emit(OP_BUILD_LIST, static_cast<int>(n->elements.size()));
    } else {
        emit(OP_BUILD_LIST, 0);
        for (auto& e : n->elements) {
            if (auto* s = dynamic_cast<StarredNode*>(e.get())) {
                if (!compileNode(s->value.get())) return false;
                emit(OP_LIST_EXTEND, 1);
                continue;
            }
            if (!compileNode(e.get())) return false;
            emit(OP_LIST_APPEND, 1);
        }
    }
    return true;
}

bool Compiler::compileDictLiteral(DictLiteralNode* n) {
    if (!n) return false;
    bool hasUnpacking = false;
    for (auto& k : n->keys) {
        if (k == nullptr) { hasUnpacking = true; break; }
    }

    if (!hasUnpacking) {
        for (size_t i = 0; i < n->keys.size(); ++i) {
            if (!compileNode(n->keys[i].get()) || !compileNode(n->values[i].get())) return false;
        }
        emit(OP_BUILD_MAP, static_cast<int>(n->keys.size()));
    } else {
        emit(OP_BUILD_MAP, 0);
        for (size_t i = 0; i < n->keys.size(); ++i) {
            if (n->keys[i] == nullptr) {
                // Unpacking: **v
                if (auto* u = dynamic_cast<StarredNode*>(n->values[i].get())) {
                    if (!compileNode(u->value.get())) return false;
                    emit(OP_DICT_UPDATE, 1);
                } else if (auto* uold = dynamic_cast<UnaryOpNode*>(n->values[i].get())) {
                    // Fallback for old UnaryOpNode if any
                     if (!compileNode(uold->operand.get())) return false;
                     emit(OP_DICT_UPDATE, 1);
                }
            } else {
                // OP_MAP_ADD impl reads `key` from TOS and `val` from TOS-1,
                // and resolves the dict at stack[size - arg - 1]. Push value
                // first then key, so TOS = key matches what the impl expects;
                // arg = 2 puts the dict three slots below the top, where it
                // actually lives (BUILD_MAP pushed it before this loop).
                if (!compileNode(n->values[i].get()) || !compileNode(n->keys[i].get())) return false;
                emit(OP_MAP_ADD, 2);
            }
        }
    }
    return true;
}

bool Compiler::compileTupleLiteral(TupleLiteralNode* n) {
    if (!n) return false;
    bool hasStarred = false;
    for (auto& e : n->elements) {
        if (dynamic_cast<StarredNode*>(e.get())) { hasStarred = true; break; }
    }

    if (!hasStarred) {
        for (auto& e : n->elements) {
            if (!compileNode(e.get())) return false;
        }
        emit(OP_BUILD_TUPLE, static_cast<int>(n->elements.size()));
    } else {
        // Tuples are slightly harder since we can't easily extend them
        // So we build a list and convert to tuple at the end.
        emit(OP_BUILD_LIST, 0);
        for (auto& e : n->elements) {
            if (auto* s = dynamic_cast<StarredNode*>(e.get())) {
                if (!compileNode(s->value.get())) return false;
                emit(OP_LIST_EXTEND, 1);
                continue;
            }
            if (!compileNode(e.get())) return false;
            emit(OP_LIST_APPEND, 1);
        }
        emit(OP_LIST_TO_TUPLE, 0);
    }
    return true;
}

bool Compiler::compileSetLiteral(SetLiteralNode* n) {
    if (!n) return false;
    bool hasStarred = false;
    for (auto& e : n->elements) {
        if (dynamic_cast<StarredNode*>(e.get())) { hasStarred = true; break; }
    }

    if (!hasStarred) {
        for (auto& e : n->elements) {
            if (!compileNode(e.get())) return false;
        }
        emit(OP_BUILD_SET, static_cast<int>(n->elements.size()));
    } else {
        emit(OP_BUILD_SET, 0);
        for (auto& e : n->elements) {
            if (auto* s = dynamic_cast<StarredNode*>(e.get())) {
                if (!compileNode(s->value.get())) return false;
                emit(OP_SET_UPDATE, 1);
                continue;
            }
            if (!compileNode(e.get())) return false;
            emit(OP_SET_ADD, 1);
        }
    }
    return true;
}

std::string Compiler::mangleIdentifier(const std::string& raw) const {
    if (currentClassName_.empty()) return raw;
    if (raw.size() < 2 || raw[0] != '_' || raw[1] != '_') return raw;
    // Names ending in `__` are dunders — never mangled.
    if (raw.size() >= 4 && raw[raw.size() - 1] == '_' && raw[raw.size() - 2] == '_') return raw;
    std::string cls = currentClassName_;
    size_t i = 0;
    while (i < cls.size() && cls[i] == '_') ++i;
    cls = cls.substr(i);
    if (cls.empty()) return raw;
    return "_" + cls + raw;
}

bool Compiler::emitNameOp(const std::string& rawId, TargetCtx ctx, bool pushNull) {
    // Apply CPython name-mangling for `__name` references inside a
    // class body or method.  The mangled name then participates
    // identically in scope lookup (locals/globals/nonlocals) and
    // bytecode emission, so the compiler treats `_C__name` and
    // `__name` as the same identifier consistently.
    const std::string id = mangleIdentifier(rawId);
    if (nonlocalNames_.count(id)) {
        int idx = addName(id);
        int op = OP_LOAD_DEREF;
        if (ctx == TargetCtx::Store) op = OP_STORE_DEREF;
        // For function calls, push NULL marker before the callable (3.11+ calling convention)
        if (ctx == TargetCtx::Load && pushNull) emit(OP_PUSH_NULL, 0);
        emit(op, idx);
        return true;
    }
    if (!forceMapped_) {
        auto it = localSlotMap_.find(id);
        if (it != localSlotMap_.end()) {
            int op = OP_LOAD_FAST;
            if (ctx == TargetCtx::Store) op = OP_STORE_FAST;
            else if (ctx == TargetCtx::Delete) op = OP_DELETE_FAST;
            // For function calls, push NULL marker before the callable (3.11+ calling convention)
            if (op == OP_LOAD_FAST && pushNull) emit(OP_PUSH_NULL, 0);
            emit(op, it->second);
            return true;
        }
    }

    int idx = addName(id);
    if (globalNames_.count(id)) {
        int op = (ctx == TargetCtx::Load) ? OP_LOAD_GLOBAL : (ctx == TargetCtx::Store ? OP_STORE_GLOBAL : OP_DELETE_GLOBAL);
        int arg = (idx << 1) | ((op == OP_LOAD_GLOBAL && pushNull) ? 1 : 0);
        emit(op, arg);
        return true;
    }
    // In function scope, unresolved names are globals/builtins — use LOAD_GLOBAL for a faster
    // lookup path (skips frame->getAttribute, goes straight to env->resolve() ptrCache).
    // At module and class scope, LOAD_NAME is correct (local namespace dict must be checked first).
    if (isFunctionScope_ && !isClassBody_ && !forceMapped_) {
        int gop = (ctx == TargetCtx::Load) ? OP_LOAD_GLOBAL : (ctx == TargetCtx::Store ? OP_STORE_GLOBAL : OP_DELETE_GLOBAL);
        int garg = (idx << 1) | ((gop == OP_LOAD_GLOBAL && pushNull) ? 1 : 0);
        emit(gop, garg);
        return true;
    }
    int op = (ctx == TargetCtx::Load) ? OP_LOAD_NAME : (ctx == TargetCtx::Store ? OP_STORE_NAME : OP_DELETE_NAME);
    int arg = (idx << 1) | ((op == OP_LOAD_NAME && pushNull) ? 1 : 0);
    emit(op, arg);
    return true;
}

bool Compiler::compileTarget(ASTNode* target, TargetCtx ctx) {
    if (!target) return false;
    if (auto* nm = dynamic_cast<NameNode*>(target)) {
        return emitNameOp(nm->id, ctx);
    }
    if (auto* att = dynamic_cast<AttributeNode*>(target)) {
        if (!compileNode(att->value.get())) return false;
        // PEP 8 private-name mangling — same as compileAttribute load path.
        int idx = addName(mangleIdentifier(att->attr));
        if (ctx == TargetCtx::Store)
            emit(OP_STORE_ATTR, (idx << 1));
        else if (ctx == TargetCtx::Delete)
            emit(OP_DELETE_ATTR, (idx << 1));
        else
            emit(OP_LOAD_ATTR, (idx << 1) | 1);
        return true;
    }
    if (auto* sub = dynamic_cast<SubscriptNode*>(target)) {
        if (!compileNode(sub->value.get()) || !compileNode(sub->index.get())) return false;
        if (ctx == TargetCtx::Store) {
            emit(OP_STORE_SUBSCR, 0);
        } else if (ctx == TargetCtx::Delete) {
            emit(OP_DELETE_SUBSCR, 0);
        } else {
            emit(OP_BINARY_SUBSCR, 0);
        }
        return true;
    }
    if (auto* tup = dynamic_cast<TupleLiteralNode*>(target)) {
        if (ctx == TargetCtx::Store) {
            int starredIdx = -1;
            for (int i = 0; i < static_cast<int>(tup->elements.size()); ++i) {
                if (dynamic_cast<StarredNode*>(tup->elements[i].get())) {
                    starredIdx = i;
                    break;
                }
            }
            if (starredIdx == -1) {
                emit(OP_UNPACK_SEQUENCE, static_cast<int>(tup->elements.size()));
            } else {
                int before = starredIdx;
                int after = static_cast<int>(tup->elements.size()) - 1 - starredIdx;
                emit(OP_UNPACK_EX, (after << 8) | before);
            }
        } else if (ctx != TargetCtx::Delete) {
            return false;
        }
        for (auto& e : tup->elements) {
            if (auto* sn = dynamic_cast<StarredNode*>(e.get())) {
                if (!compileTarget(sn->value.get(), ctx)) return false;
            } else {
                if (!compileTarget(e.get(), ctx)) return false;
            }
        }
        return true;
    }
    if (auto* lst = dynamic_cast<ListLiteralNode*>(target)) {
        if (ctx == TargetCtx::Store) {
            int starredIdx = -1;
            for (int i = 0; i < static_cast<int>(lst->elements.size()); ++i) {
                if (dynamic_cast<StarredNode*>(lst->elements[i].get())) {
                    starredIdx = i;
                    break;
                }
            }
            if (starredIdx == -1) {
                emit(OP_UNPACK_SEQUENCE, static_cast<int>(lst->elements.size()));
            } else {
                int before = starredIdx;
                int after = static_cast<int>(lst->elements.size()) - 1 - starredIdx;
                emit(OP_UNPACK_EX, (after << 8) | before);
            }
        } else if (ctx != TargetCtx::Delete) {
            return false;
        }
        for (auto& e : lst->elements) {
            if (auto* sn = dynamic_cast<StarredNode*>(e.get())) {
                if (!compileTarget(sn->value.get(), ctx)) return false;
            } else {
                if (!compileTarget(e.get(), ctx)) return false;
            }
        }
        return true;
    }
    return false;
}

bool Compiler::compileAssign(AssignNode* n) {
    if (!n || n->targets.empty()) return false;
    if (!compileNode(n->value.get())) return false;
    for (size_t i = 0; i < n->targets.size(); ++i) {
        if (i + 1 < n->targets.size()) {
            emit(OP_DUP_TOP, 0);
        }
        if (!compileTarget(n->targets[i].get(), TargetCtx::Store)) return false;
    }
    return true;
}

bool Compiler::compileNamedExpr(NamedExprNode* n) {
    if (!n || !compileNode(n->value.get())) return false;
    emit(OP_DUP_TOP, 0);
    if (!compileTarget(n->target.get(), TargetCtx::Store)) return false;
    return true;
}

bool Compiler::compileAnnAssign(AnnAssignNode* n) {
    if (!n) return false;
    // CPython restricts the target of an annotated assignment to a plain
    // NAME, attribute access, or subscript.  Patterns like `[x, 0]: int`,
    // `f(): int`, `(x,): int` must raise SyntaxError.
    if (n->target) {
        ASTNode* t = n->target.get();
        bool valid = dynamic_cast<NameNode*>(t) != nullptr
                  || dynamic_cast<AttributeNode*>(t) != nullptr
                  || dynamic_cast<SubscriptNode*>(t) != nullptr;
        if (!valid) return false;
    }
    if (n->value) {
        if (!compileNode(n->value.get())) return false;
        if (!compileTarget(n->target.get(), TargetCtx::Store)) return false;
    }
    // In a class body, store the annotation into __annotations__['name'] = type.
    // STORE_SUBSCR expects stack: [value, container, key(TOS)] → container[key] = value.
    if (isClassBody_) {
        auto* nm = dynamic_cast<NameNode*>(n->target.get());
        if (nm) {
            if (!compileNode(n->annotation.get())) return false;  // value
            emitNameOp("__annotations__", TargetCtx::Load);       // container
            // CPython name mangling: in a class body, a name starting
            // with `__` but not ending with `__` is rewritten to
            // `_<ClassName>__<name>` (with leading underscores of
            // ClassName stripped).  Apply the same mangling to the
            // dictionary key so `class C: __foo: int` ends up at
            // `C.__annotations__['_C__foo']` matching CPython.
            std::string keyName = nm->id;
            if (!currentClassName_.empty()
                && keyName.size() >= 2
                && keyName[0] == '_' && keyName[1] == '_'
                && !(keyName.size() >= 4
                     && keyName[keyName.size() - 1] == '_'
                     && keyName[keyName.size() - 2] == '_')) {
                std::string cls = currentClassName_;
                size_t i = 0;
                while (i < cls.size() && cls[i] == '_') ++i;
                cls = cls.substr(i);
                if (!cls.empty()) {
                    keyName = "_" + cls + keyName;
                }
            }
            int keyIdx = addConstant(PythonEnvironment::getInternedString(ctx_, keyName.c_str())->asObject(ctx_));
            emit(OP_LOAD_CONST, keyIdx);                           // key (TOS)
            emit(OP_STORE_SUBSCR);
        }
    } else if (!isFunctionScope_) {
        // Module-level annotation: `x: int = 5` at the top of a module
        // populates the module's __annotations__ dict.  Wrap each
        // expression evaluation in SETUP_FINALLY so a forward reference
        // doesn't abort module load.  Parenthesized targets `(x): T`
        // do NOT contribute to __annotations__ (consistent with the
        // local-binding rule in collectAnnotationOnlyLocals).
        auto* nm = dynamic_cast<NameNode*>(n->target.get());
        if (nm && !nm->parenthesized) {
            emit(OP_SETUP_FINALLY, 0);
            int handlerSlot = bytecodeOffset() - 1;
            if (!compileNode(n->annotation.get())) return false;        // value
            emitNameOp("__annotations__", TargetCtx::Load);             // container
            int keyIdx = addConstant(PythonEnvironment::getInternedString(ctx_, nm->id.c_str())->asObject(ctx_));
            emit(OP_LOAD_CONST, keyIdx);                                 // key (TOS)
            emit(OP_STORE_SUBSCR);
            emit(OP_POP_BLOCK);
            emit(OP_JUMP_ABSOLUTE, 0);
            int doneSlot = bytecodeOffset() - 1;
            // Handler: drop the 3 exception items from the stack.
            addPatch(handlerSlot, bytecodeOffset());
            emit(OP_POP_TOP);
            emit(OP_POP_TOP);
            emit(OP_POP_TOP);
            addPatch(doneSlot, bytecodeOffset());
        }
    }
    return true;
}

bool Compiler::compileDeleteNode(DeleteNode* n) {
    if (!n) return false;
    for (auto& target : n->targets) {
        if (!compileTarget(target.get(), TargetCtx::Delete)) return false;
    }
    return true;
}

bool Compiler::compileAssert(AssertNode* n) {
    if (!n) return false;
    // CPython emits a SyntaxWarning when the assertion test is a non-empty
    // tuple literal — `assert (cond, msg)` is a common typo for the two-arg
    // form `assert cond, msg`, but the tuple is always truthy so the
    // assertion never fires.  See test_assert_syntax_warnings /
    // test_assert_warning_promotes_to_syntax_error.
    if (auto* t = dynamic_cast<TupleLiteralNode*>(n->test.get())) {
        // The empty tuple `()` is also always-falsy by Python semantics, but
        // CPython still warns for any tuple literal in this position because
        // the intent is almost certainly the two-arg form.  Empty tuples
        // would assert(False)-like.  test_assert_syntax_warnings only
        // exercises the non-empty case, so we mirror that for now (warn
        // only when the tuple has at least one element).
        if (!t->elements.empty()) {
            std::string msg = "assertion is always true, perhaps remove parentheses?";
            if (PythonEnvironment* env = PythonEnvironment::fromContext(ctx_)) {
                int line = n->line > 0 ? n->line : 1;
                if (env->emitSyntaxWarning(ctx_, msg, filename_, line)) {
                    return false;
                }
            }
        }
    }
    if (!compileNode(n->test.get())) return false;
    // if test is true, jump to end
    emit(OP_POP_JUMP_IF_TRUE, 0);
    int jumpToEndSlot = bytecodeOffset() - 1;
    
    // if test is false, raise AssertionError
    int idxAlt = addName("AssertionError");
    emit(OP_LOAD_GLOBAL, (idxAlt << 1) | 1);
    if (n->msg) {
        if (!compileNode(n->msg.get())) return false;
        emit(OP_CALL_FUNCTION, 1);
    } else {
        emit(OP_CALL_FUNCTION, 0);
    }
    emit(OP_RAISE_VARARGS, 1);
    
    int endTarget = bytecodeOffset();
    addPatch(jumpToEndSlot, endTarget);
    return true;
}

bool Compiler::compileWhile(WhileNode* n) {
    if (!n || !n->test) return false;
    int startPC = bytecodeOffset();
    
    loopStack_.push_back({startPC, {}, blockEnvStack_.size(), false});
    
    if (!compileNode(n->test.get())) return false;
    
    emit(OP_POP_JUMP_IF_FALSE, 0);
    int jumpToEndSlot = bytecodeOffset() - 1;
    
    if (!compileNode(n->body.get())) return false;
    emit(OP_JUMP_ABSOLUTE, startPC * 2);
    
    int afterLoopBody = bytecodeOffset();
    addPatch(jumpToEndSlot, afterLoopBody);
    
    if (n->orelse) {
        if (!compileNode(n->orelse.get())) return false;
    }
    
    int endPC = bytecodeOffset();
    for (int patchIdx : loopStack_.back().breakPatches) {
        addPatch(patchIdx, endPC);
    }
    loopStack_.pop_back();
    
    return true;
}

bool Compiler::compileFor(ForNode* n) {
    if (!n || !compileNode(n->iter.get())) return false;
    emit(OP_GET_ITER);
    int loopStart = bytecodeOffset();
    
    loopStack_.push_back({loopStart, {}, blockEnvStack_.size(), true});
    
    emit(OP_FOR_ITER, 0);
    int argSlot = bytecodeOffset() - 1;
    if (!compileTarget(n->target.get(), TargetCtx::Store)) return false;
    if (!compileNode(n->body.get())) return false;
    emit(OP_JUMP_ABSOLUTE, loopStart * 2);
    int afterLoop = bytecodeOffset();
    addPatch(argSlot, afterLoop);
    
    if (n->orelse) {
        if (!compileNode(n->orelse.get())) return false;
    }
    
    for (int patchIdx : loopStack_.back().breakPatches) {
        addPatch(patchIdx, bytecodeOffset());
    }
    loopStack_.pop_back();
    
    return true;
}

bool Compiler::compileBreak(BreakNode* n) {
    if (loopStack_.empty()) return false;
    // When this break is being compiled INSIDE a finally body that the
    // compiler is replaying for a pending `return`, the return value
    // is sitting on the operand stack.  Discard it before redirecting
    // — break suppresses the return per CPython semantics, and leaving
    // the value behind would corrupt the surrounding loop's iterator
    // (FOR_ITER pops what it thinks is the iter on the next iteration).
    for (int i = 0; i < returnUnwindDepth_; ++i) emit(OP_POP_TOP, 0);
    if (!unwindBlocks(true)) return false;
    if (loopStack_.back().hasIterator) {
        emit(OP_POP_TOP, 0);
    }
    emit(OP_JUMP_ABSOLUTE, 0);
    loopStack_.back().breakPatches.push_back(bytecodeOffset() - 1);
    return true;
}

bool Compiler::compileContinue(ContinueNode* n) {
    if (loopStack_.empty()) return false;
    // Same rationale as compileBreak: discard any pending return value
    // when continuing out of a finally compiled for `return`.
    for (int i = 0; i < returnUnwindDepth_; ++i) emit(OP_POP_TOP, 0);
    if (!unwindBlocks(true)) return false;
    emit(OP_JUMP_ABSOLUTE, loopStack_.back().start * 2);
    return true;
}

bool Compiler::compileIf(IfNode* n) {
    if (!n || !compileNode(n->test.get())) return false;
    emit(OP_POP_JUMP_IF_FALSE, 0);
    int elseSlot = bytecodeOffset() - 1;
    if (!compileNode(n->body.get())) return false;
    int elseTarget = bytecodeOffset();
    if (n->orelse) {
        emit(OP_JUMP_ABSOLUTE, 0);
        int endSlot = bytecodeOffset() - 1;
        addPatch(elseSlot, bytecodeOffset()); // Jump to start of else
        if (!compileNode(n->orelse.get())) return false;
        addPatch(endSlot, bytecodeOffset()); // End of body skips else
    } else {
        addPatch(elseSlot, elseTarget);
    }
    return true;
}

bool Compiler::compileGlobal(GlobalNode* n) {
    if (!n) return false;
    for (const auto& name : n->names)
        globalNames_.insert(name);
    return true;
}

bool Compiler::compileReturn(ReturnNode* n) {
    // `return` is only legal inside a function body.  At module / class
    // scope it must raise SyntaxError.
    if (!isFunctionScope_ || isClassBody_) {
        return false;  // compileNode will surface this as SyntaxError via py_compile
    }
    if (n->value) {
        if (!compileNode(n->value.get())) return false;
    } else {
        int idx = addConstant(PROTO_NONE);
        emit(OP_LOAD_CONST, idx);
    }
    if (!unwindBlocks(false, /*hasValueOnStack=*/true)) return false;
    emit(OP_RETURN_VALUE);
    return true;
}

bool Compiler::compileYield(YieldNode* n) {
    // Same rule as `return`: `yield` / `yield from` are only legal inside
    // a function body (which, once we see a yield, becomes a generator).
    if (!isFunctionScope_ || isClassBody_) {
        return false;
    }
    // PC2 (PEP 525): `yield from` is forbidden inside `async def`.
    // (Plain `yield` IS allowed — that's what makes the function an
    // async generator.)
    if (n->isFrom && isAsyncFunction_) {
        if (auto* env = PythonEnvironment::getCurrentEnvironment()) {
            env->raiseSyntaxError(ctx_,
                "'yield from' is not allowed in an async function",
                n->line, 0, std::string());
        }
        return false;
    }
    isGenerator_ = true;
    if (n->value) {
        if (!compileNode(n->value.get())) return false;
    } else {
        int idx = addConstant(PROTO_NONE);
        emit(OP_LOAD_CONST, idx);
    }
    
    if (n->isFrom) {
        emit(OP_GET_YIELD_FROM_ITER);
        int noneIdx = addConstant(PROTO_NONE);
        emit(OP_LOAD_CONST, noneIdx); // Initial send value is None
        emit(OP_YIELD_FROM);
    } else {
        emit(OP_YIELD_VALUE);
    }
    return true;
}

// Recursively scan an AST subtree for any YieldNode.  Used to reject
// `yield` / `yield from` inside list/set/dict comprehensions and
// generator expressions (CPython raises SyntaxError with a kind-specific
// message for each of these).  We deliberately stop at nested function
// definitions since a `yield` there belongs to the inner function, not
// the comprehension.
static bool astContainsYield(ASTNode* node) {
    if (!node) return false;
    if (dynamic_cast<YieldNode*>(node)) return true;
    if (dynamic_cast<FunctionDefNode*>(node)) return false;
    if (dynamic_cast<LambdaNode*>(node)) return false;
    if (auto* s = dynamic_cast<SuiteNode*>(node)) {
        for (auto& st : s->statements) if (astContainsYield(st.get())) return true;
        return false;
    }
    if (auto* c = dynamic_cast<CallNode*>(node)) {
        if (astContainsYield(c->func.get())) return true;
        for (auto& a : c->args) if (astContainsYield(a.get())) return true;
        for (auto& kw : c->keywords) if (astContainsYield(kw.second.get())) return true;
        return false;
    }
    if (auto* b = dynamic_cast<BinOpNode*>(node))
        return astContainsYield(b->left.get()) || astContainsYield(b->right.get());
    if (auto* u = dynamic_cast<UnaryOpNode*>(node))
        return astContainsYield(u->operand.get());
    if (auto* sub = dynamic_cast<SubscriptNode*>(node))
        return astContainsYield(sub->value.get()) || astContainsYield(sub->index.get());
    if (auto* att = dynamic_cast<AttributeNode*>(node))
        return astContainsYield(att->value.get());
    if (auto* sl = dynamic_cast<SliceNode*>(node))
        return astContainsYield(sl->start.get()) || astContainsYield(sl->stop.get()) || astContainsYield(sl->step.get());
    if (auto* iff = dynamic_cast<ConditionalExprNode*>(node))
        return astContainsYield(iff->test.get()) || astContainsYield(iff->body.get()) || astContainsYield(iff->orelse.get());
    if (auto* tup = dynamic_cast<TupleLiteralNode*>(node)) {
        for (auto& e : tup->elements) if (astContainsYield(e.get())) return true;
        return false;
    }
    if (auto* lst = dynamic_cast<ListLiteralNode*>(node)) {
        for (auto& e : lst->elements) if (astContainsYield(e.get())) return true;
        return false;
    }
    if (auto* st = dynamic_cast<StarredNode*>(node))
        return astContainsYield(st->value.get());
    if (auto* ne = dynamic_cast<NamedExprNode*>(node))
        return astContainsYield(ne->value.get());
    if (auto* ret = dynamic_cast<ReturnNode*>(node))
        return astContainsYield(ret->value.get());
    return false;
}

// Returns true if the comprehension body, any filter, or any iter/elt
// references a yield.  The *first* generator's iter is evaluated in the
// enclosing scope, so we exclude it intentionally (matches CPython).
static bool compHasInternalYield(const std::vector<Comprehension>& gens, ASTNode* elt, ASTNode* value = nullptr) {
    if (elt && astContainsYield(elt)) return true;
    if (value && astContainsYield(value)) return true;
    for (size_t i = 0; i < gens.size(); ++i) {
        if (i > 0 && astContainsYield(gens[i].iter.get())) return true;
        for (const auto& ifn : gens[i].ifs) if (astContainsYield(ifn.get())) return true;
    }
    return false;
}

bool Compiler::compileListComp(ListCompNode* n) {
    if (!n) return false;
    if (compHasInternalYield(n->generators, n->elt.get())) return false;

    // Python 3: List comprehensions have their own scope.
    // Emit using the 3.11+ [NULL, callable, arg] calling convention so the inner
    // CALL_FUNCTION 1 always finds the NULL marker at the right stack position,
    // regardless of what the outer call frame has pushed.
    // Sequence: PUSH_NULL, LOAD_CONST(code), BUILD_FUNCTION, eval_iter, GET_ITER, CALL_FUNCTION 1

    Compiler bodyCompiler(ctx_, filename_);
    bodyCompiler.localSlotMap_[".0"] = 0;
    bodyCompiler.globalNames_ = globalNames_;
    bodyCompiler.isFunctionScope_ = true;

    // Collect locals and nonlocals for comprehension scope
    std::unordered_set<std::string> compLocals;
    for (const auto& gen : n->generators) collectDefinedNames(gen.target.get(), compLocals);
    
    std::unordered_set<std::string> compUsed;
    for (const auto& gen : n->generators) {
        collectUsedNames(gen.iter.get(), compUsed);
        collectUsedNames(gen.target.get(), compUsed);
        for (const auto& i : gen.ifs) collectUsedNames(i.get(), compUsed);
    }
    collectUsedNames(n->elt.get(), compUsed);
    
    bool isAsync = false;
    for (const auto& gen : n->generators) if (gen.is_async) { isAsync = true; break; }

    for (const auto& name : compUsed) {
        if (!compLocals.count(name) && !bodyCompiler.globalNames_.count(name)) {
            // Only capture as a nonlocal (LOAD_DEREF) if the name is actually a
            // local or nonlocal in the directly enclosing function scope.
            // Names not found in the enclosing locals/nonlocals resolve via
            // LOAD_GLOBAL (module-level globals, builtins, etc.).
            // definedLocals_ covers the case where the enclosing function is
            // forceMapped (its localSlotMap_ is empty) but still defines the
            // name as a local on the frame.
            if (localSlotMap_.count(name) || definedLocals_.count(name) || nonlocalNames_.count(name)) {
                bodyCompiler.nonlocalNames_.insert(name);
            }
        }
    }

    std::vector<std::string> orderedLocals = {".0"};
    int slot = 1;
    for (const auto& name : compLocals) {
        if (bodyCompiler.localSlotMap_.find(name) == bodyCompiler.localSlotMap_.end()) {
            bodyCompiler.localSlotMap_[name] = slot++;
            orderedLocals.push_back(name);
        }
    }
    for (const auto& name : orderedLocals) bodyCompiler.definedLocals_.insert(name);

    // Listcomp accumulator: use a raw ProtoList (not a wrapped
    // `list` instance) for the duration of the body. The wrapped
    // variant goes through `__data__` setAttribute on a mutable
    // object every LIST_APPEND, which under heavy load (≥80×80
    // nested with bytes operands) hits a protoCore mutable-shard GC
    // corruption that drops the accumulator's __data__ to PROTO_NONE
    // and produces a ZERO-length result list. The raw form skips
    // that round-trip entirely; OP_LIST_APPEND's raw fast-path
    // accepts a bare ProtoList on the stack. We wrap once at body-
    // end via OP_WRAP_RAW_LIST so callers continue to receive a
    // proper Python list.
    bodyCompiler.emit(OP_BUILD_RAW_LIST, 0);

    auto oldIter = std::move(n->generators[0].iter);
    auto itNode = std::make_unique<NameNode>();
    itNode->id = ".0";
    n->generators[0].iter = std::move(itNode);

    int nGen = static_cast<int>(n->generators.size());
    auto innerOk = bodyCompiler.compileComprehension(n->generators, 0, [&]() {
        if (!bodyCompiler.compileNode(n->elt.get())) return false;
        bodyCompiler.emit(OP_LIST_APPEND, nGen + 1);
        return true;
    });

    n->generators[0].iter = std::move(oldIter);
    if (!innerOk) return false;

    bodyCompiler.emit(OP_WRAP_RAW_LIST, 0);
    bodyCompiler.emit(OP_RETURN_VALUE);
    bodyCompiler.applyPatches();
    
    // Create code object
    std::vector<const proto::ProtoObject*> varnamesVec;
    varnamesVec.reserve(orderedLocals.size());
    for (const auto& name : orderedLocals)
        varnamesVec.push_back(PythonEnvironment::getInternedString(ctx_, name.c_str())->asObject(ctx_));
    const proto::ProtoTuple* co_varnames = ctx_->newTuple(varnamesVec);
    
    int flags = CO_OPTIMIZED | CO_NEWLOCALS;
    if (isAsync) flags |= 256; // CO_COROUTINE (CPython 0x100, matches inspect.CO_COROUTINE)

    const proto::ProtoObject* codeObj = makeCodeObject(ctx_, 
        bodyCompiler.getConstants(), bodyCompiler.getNames(), bodyCompiler.getBytecode(), 
        PythonEnvironment::getInternedString(ctx_, filename_.c_str()), 
        co_varnames, 1, 0, static_cast<int>(orderedLocals.size()), 
        flags, false, 
        PythonEnvironment::getInternedString(ctx_, "<listcomp>"),
        bodyCompiler.getFirstLine(), bodyCompiler.getLnotab());
    // 3.11+ calling convention: [NULL, callable, arg]
    emit(OP_PUSH_NULL);
    emit(OP_LOAD_CONST, addConstant(codeObj));
    emit(OP_BUILD_FUNCTION, 0);
    if (!compileNode(n->generators[0].iter.get())) return false;
    emit(OP_GET_ITER);
    emit(OP_CALL_FUNCTION, 1);

    if (isAsync) {
        emit(OP_GET_AWAITABLE);
        emit(OP_LOAD_CONST, addConstant(PROTO_NONE));
        emit(OP_YIELD_FROM);
    }

    return true;
}

bool Compiler::compileDictComp(DictCompNode* n) {
    if (!n) return false;
    if (compHasInternalYield(n->generators, n->key.get(), n->value.get())) return false;

    // Emit using 3.11+ [NULL, callable, arg] calling convention — see compileListComp.

    Compiler bodyCompiler(ctx_, filename_);
    bodyCompiler.localSlotMap_[".0"] = 0;
    bodyCompiler.globalNames_ = globalNames_;
    bodyCompiler.isFunctionScope_ = true;

    // Collect locals and nonlocals for comprehension scope
    std::unordered_set<std::string> compLocals;
    for (const auto& gen : n->generators) collectDefinedNames(gen.target.get(), compLocals);
    
    std::unordered_set<std::string> compUsed;
    for (const auto& gen : n->generators) {
        collectUsedNames(gen.iter.get(), compUsed);
        collectUsedNames(gen.target.get(), compUsed);
        for (const auto& i : gen.ifs) collectUsedNames(i.get(), compUsed);
    }
    collectUsedNames(n->key.get(), compUsed);
    collectUsedNames(n->value.get(), compUsed);
    
    bool isAsync = false;
    for (const auto& gen : n->generators) if (gen.is_async) { isAsync = true; break; }
    
    for (const auto& name : compUsed) {
        if (!compLocals.count(name) && !bodyCompiler.globalNames_.count(name)) {
            if (localSlotMap_.count(name) || definedLocals_.count(name) || nonlocalNames_.count(name)) {
                bodyCompiler.nonlocalNames_.insert(name);
            }
        }
    }

    std::vector<std::string> orderedLocals = {".0"};
    int slot = 1;
    for (const auto& name : compLocals) {
        if (bodyCompiler.localSlotMap_.find(name) == bodyCompiler.localSlotMap_.end()) {
            bodyCompiler.localSlotMap_[name] = slot++;
            orderedLocals.push_back(name);
        }
    }
    for (const auto& name : orderedLocals) bodyCompiler.definedLocals_.insert(name);

    bodyCompiler.emit(OP_BUILD_MAP, 0);
    
    auto oldIter = std::move(n->generators[0].iter);
    auto itNode = std::make_unique<NameNode>();
    itNode->id = ".0";
    n->generators[0].iter = std::move(itNode);
    
    int nGen = static_cast<int>(n->generators.size());
    auto innerOk = bodyCompiler.compileComprehension(n->generators, 0, [&]() {
        if (!bodyCompiler.compileNode(n->value.get())) return false;
        if (!bodyCompiler.compileNode(n->key.get())) return false;
        // MAP_ADD needs nGen+2: the stack during body is [map, iter1..iterN, value, key],
        // so the accumulator is 2 positions deeper than for LIST_APPEND/SET_ADD (which
        // push only 1 item before the instruction).
        bodyCompiler.emit(OP_MAP_ADD, nGen + 2);
        return true;
    });
    
    n->generators[0].iter = std::move(oldIter);
    if (!innerOk) return false;
    
    bodyCompiler.emit(OP_RETURN_VALUE);
    bodyCompiler.applyPatches();
    
    std::vector<const proto::ProtoObject*> varnamesVec;
    varnamesVec.reserve(orderedLocals.size());
    for (const auto& name : orderedLocals)
        varnamesVec.push_back(PythonEnvironment::getInternedString(ctx_, name.c_str())->asObject(ctx_));
    const proto::ProtoTuple* co_varnames = ctx_->newTuple(varnamesVec);
    
    int flags = CO_OPTIMIZED | CO_NEWLOCALS;
    if (isAsync) flags |= 256; // CO_COROUTINE (CPython 0x100, matches inspect.CO_COROUTINE)

    const proto::ProtoObject* codeObj = makeCodeObject(ctx_, 
        bodyCompiler.getConstants(), bodyCompiler.getNames(), bodyCompiler.getBytecode(), 
        PythonEnvironment::getInternedString(ctx_, filename_.c_str()), 
        co_varnames, 1, 0, static_cast<int>(orderedLocals.size()), 
        flags, false, 
        PythonEnvironment::getInternedString(ctx_, "<dictcomp>"),
        bodyCompiler.getFirstLine(), bodyCompiler.getLnotab());
    // 3.11+ calling convention: [NULL, callable, arg]
    emit(OP_PUSH_NULL);
    emit(OP_LOAD_CONST, addConstant(codeObj));
    emit(OP_BUILD_FUNCTION, 0);
    if (!compileNode(n->generators[0].iter.get())) return false;
    emit(OP_GET_ITER);
    emit(OP_CALL_FUNCTION, 1);

    if (isAsync) {
        emit(OP_GET_AWAITABLE);
        emit(OP_LOAD_CONST, addConstant(PROTO_NONE));
        emit(OP_YIELD_FROM);
    }

    return true;
}

bool Compiler::compileSetComp(SetCompNode* n) {
    if (!n) return false;
    if (compHasInternalYield(n->generators, n->elt.get())) return false;

    // Emit using 3.11+ [NULL, callable, arg] calling convention — see compileListComp.

    Compiler bodyCompiler(ctx_, filename_);
    bodyCompiler.localSlotMap_[".0"] = 0;
    bodyCompiler.globalNames_ = globalNames_;
    bodyCompiler.isFunctionScope_ = true;

    // Collect locals and nonlocals for comprehension scope
    std::unordered_set<std::string> compLocals;
    for (const auto& gen : n->generators) collectDefinedNames(gen.target.get(), compLocals);
    
    std::unordered_set<std::string> compUsed;
    for (const auto& gen : n->generators) {
        collectUsedNames(gen.iter.get(), compUsed);
        collectUsedNames(gen.target.get(), compUsed);
        for (const auto& i : gen.ifs) collectUsedNames(i.get(), compUsed);
    }
    collectUsedNames(n->elt.get(), compUsed);
    
    bool isAsync = false;
    for (const auto& gen : n->generators) if (gen.is_async) { isAsync = true; break; }
    
    for (const auto& name : compUsed) {
        if (!compLocals.count(name) && !bodyCompiler.globalNames_.count(name)) {
            if (localSlotMap_.count(name) || definedLocals_.count(name) || nonlocalNames_.count(name)) {
                bodyCompiler.nonlocalNames_.insert(name);
            }
        }
    }

    std::vector<std::string> orderedLocals = {".0"};
    int slot = 1;
    for (const auto& name : compLocals) {
        if (bodyCompiler.localSlotMap_.find(name) == bodyCompiler.localSlotMap_.end()) {
            bodyCompiler.localSlotMap_[name] = slot++;
            orderedLocals.push_back(name);
        }
    }
    for (const auto& name : orderedLocals) bodyCompiler.definedLocals_.insert(name);

    bodyCompiler.emit(OP_BUILD_SET, 0);
    
    auto oldIter = std::move(n->generators[0].iter);
    auto itNode = std::make_unique<NameNode>();
    itNode->id = ".0";
    n->generators[0].iter = std::move(itNode);
    
    int nGen = static_cast<int>(n->generators.size());
    auto innerOk = bodyCompiler.compileComprehension(n->generators, 0, [&]() {
        if (!bodyCompiler.compileNode(n->elt.get())) return false;
        bodyCompiler.emit(OP_SET_ADD, nGen + 1);
        return true;
    });
    
    n->generators[0].iter = std::move(oldIter);
    if (!innerOk) return false;
    
    bodyCompiler.emit(OP_RETURN_VALUE);
    bodyCompiler.applyPatches();
    
    std::vector<const proto::ProtoObject*> varnamesVec;
    varnamesVec.reserve(orderedLocals.size());
    for (const auto& name : orderedLocals)
        varnamesVec.push_back(PythonEnvironment::getInternedString(ctx_, name.c_str())->asObject(ctx_));
    const proto::ProtoTuple* co_varnames = ctx_->newTuple(varnamesVec);
    
    int flags = CO_OPTIMIZED | CO_NEWLOCALS;
    if (isAsync) flags |= 256; // CO_COROUTINE (CPython 0x100, matches inspect.CO_COROUTINE)

    const proto::ProtoObject* codeObj = makeCodeObject(ctx_, 
        bodyCompiler.getConstants(), bodyCompiler.getNames(), bodyCompiler.getBytecode(), 
        PythonEnvironment::getInternedString(ctx_, filename_.c_str()), 
        co_varnames, 1, 0, static_cast<int>(orderedLocals.size()), 
        flags, false, 
        PythonEnvironment::getInternedString(ctx_, "<setcomp>"),
        bodyCompiler.getFirstLine(), bodyCompiler.getLnotab());
    // 3.11+ calling convention: [NULL, callable, arg]
    emit(OP_PUSH_NULL);
    emit(OP_LOAD_CONST, addConstant(codeObj));
    emit(OP_BUILD_FUNCTION, 0);
    if (!compileNode(n->generators[0].iter.get())) return false;
    emit(OP_GET_ITER);
    emit(OP_CALL_FUNCTION, 1);

    if (isAsync) {
        emit(OP_GET_AWAITABLE);
        emit(OP_LOAD_CONST, addConstant(PROTO_NONE));
        emit(OP_YIELD_FROM);
    }

    return true;
}

bool Compiler::compileGeneratorExp(GeneratorExpNode* n) {
    if (!n) return false;
    if (compHasInternalYield(n->generators, n->elt.get())) return false;

    // Emit using 3.11+ [NULL, callable, arg] calling convention — see compileListComp.

    Compiler bodyCompiler(ctx_, filename_);
    bodyCompiler.isGenerator_ = true;
    bodyCompiler.localSlotMap_[".0"] = 0;
    bodyCompiler.globalNames_ = globalNames_;
    bodyCompiler.isFunctionScope_ = true;
    
    // Collect locals and nonlocals for generator expression scope
    std::unordered_set<std::string> compLocals;
    for (const auto& gen : n->generators) collectDefinedNames(gen.target.get(), compLocals);
    
    std::unordered_set<std::string> compUsed;
    for (const auto& gen : n->generators) {
        collectUsedNames(gen.iter.get(), compUsed);
        collectUsedNames(gen.target.get(), compUsed);
        for (const auto& i : gen.ifs) collectUsedNames(i.get(), compUsed);
    }
    collectUsedNames(n->elt.get(), compUsed);
    
    bool isAsync = false;
    for (const auto& gen : n->generators) if (gen.is_async) { isAsync = true; break; }
    
    for (const auto& name : compUsed) {
        if (!compLocals.count(name) && !bodyCompiler.globalNames_.count(name)) {
            if (localSlotMap_.count(name) || definedLocals_.count(name) || nonlocalNames_.count(name)) {
                bodyCompiler.nonlocalNames_.insert(name);
            }
        }
    }

    std::vector<std::string> orderedLocals = {".0"};
    int slot = 1;
    for (const auto& name : compLocals) {
        if (bodyCompiler.localSlotMap_.find(name) == bodyCompiler.localSlotMap_.end()) {
            bodyCompiler.localSlotMap_[name] = slot++;
            orderedLocals.push_back(name);
        }
    }
    for (const auto& name : orderedLocals) bodyCompiler.definedLocals_.insert(name);

    auto oldIter = std::move(n->generators[0].iter);
    auto itNode = std::make_unique<NameNode>();
    itNode->id = ".0";
    n->generators[0].iter = std::move(itNode);

    auto innerOk = bodyCompiler.compileComprehension(n->generators, 0, [&]() {
        if (!bodyCompiler.compileNode(n->elt.get())) return false;
        bodyCompiler.emit(OP_YIELD_VALUE);
        bodyCompiler.emit(OP_POP_TOP);
        return true;
    });
    
    n->generators[0].iter = std::move(oldIter);
    if (!innerOk) return false;
    
    bodyCompiler.emit(OP_LOAD_CONST, bodyCompiler.addConstant(PROTO_NONE));
    bodyCompiler.emit(OP_RETURN_VALUE);
    bodyCompiler.applyPatches();
    
    std::vector<const proto::ProtoObject*> varnamesVec;
    varnamesVec.reserve(orderedLocals.size());
    for (const auto& name : orderedLocals)
        varnamesVec.push_back(PythonEnvironment::getInternedString(ctx_, name.c_str())->asObject(ctx_));
    const proto::ProtoTuple* co_varnames = ctx_->newTuple(varnamesVec);
    
    int flags = CO_OPTIMIZED | CO_NEWLOCALS;
    if (isAsync) flags |= 256; // CO_COROUTINE (CPython 0x100, matches inspect.CO_COROUTINE)

    const proto::ProtoObject* codeObj = makeCodeObject(ctx_, 
        bodyCompiler.getConstants(), bodyCompiler.getNames(), bodyCompiler.getBytecode(), 
        PythonEnvironment::getInternedString(ctx_, filename_.c_str()), 
        co_varnames, 1, 0, static_cast<int>(orderedLocals.size()) + bodyCompiler.getMaxStack() + 128,
        flags, true,
        PythonEnvironment::getInternedString(ctx_, "<genexpr>"),
        bodyCompiler.getFirstLine(), bodyCompiler.getLnotab());
    // 3.11+ calling convention: [NULL, callable, arg]
    emit(OP_PUSH_NULL);
    emit(OP_LOAD_CONST, addConstant(codeObj));
    emit(OP_BUILD_FUNCTION, 0);
    if (!compileNode(n->generators[0].iter.get())) return false;
    emit(OP_GET_ITER);
    emit(OP_CALL_FUNCTION, 1);

    if (isAsync) {
        emit(OP_GET_AWAITABLE);
        emit(OP_LOAD_CONST, addConstant(PROTO_NONE));
        emit(OP_YIELD_FROM);
    }

    return true;
}

bool Compiler::compileComprehension(const std::vector<Comprehension>& generators, size_t index, std::function<bool()> innerBody) {
    if (index == generators.size()) {
        return innerBody();
    }
    
    const auto& gen = generators[index];
    int loopStart = -1;
    int endSlot = -1;

    if (gen.is_async) {
        if (!compileNode(gen.iter.get())) return false;
        emit(OP_GET_AITER);
        loopStart = bytecodeOffset();
        
        int setupFinallySlot = bytecodeOffset();
        emit(OP_SETUP_FINALLY, 0);
        
        emit(OP_GET_ANEXT);
        emit(OP_GET_AWAITABLE);
        emit(OP_LOAD_CONST, addConstant(PROTO_NONE));
        emit(OP_YIELD_FROM);
        
        emit(OP_POP_BLOCK);
        if (!compileTarget(gen.target.get(), TargetCtx::Store)) return false;
        
        int jumpToBodySlot = bytecodeOffset();
        emit(OP_JUMP_FORWARD, 0);

        // Handler
        int handlerTarget = bytecodeOffset();
        addPatch(setupFinallySlot + 1, handlerTarget);
        
        int idx = addName("StopAsyncIteration");
        emit(OP_LOAD_GLOBAL, (idx << 1) | 1);
        emit(OP_EXCEPTION_MATCH);
        
        int popJumpSlot = bytecodeOffset();
        emit(OP_POP_JUMP_IF_FALSE, 0);
        
        emit(OP_POP_TOP); // exc
        emit(OP_POP_TOP); // val
        emit(OP_POP_TOP); // tb
        emit(OP_POP_EXCEPT);
        
        int afterLoopSlot = bytecodeOffset();
        emit(OP_JUMP_ABSOLUTE, 0); // Need to patch this to after the loop
        endSlot = bytecodeOffset() - 1;

        // Not StopAsyncIteration
        int reRaiseTarget = bytecodeOffset();
        addPatch(popJumpSlot + 1, reRaiseTarget);
        emit(OP_RERAISE, 0);

        // Success branch
        int successTarget = bytecodeOffset();
        addPatch(jumpToBodySlot + 1, successTarget);
    } else {
        if (!compileNode(gen.iter.get())) return false;
        emit(OP_GET_ITER);
        loopStart = bytecodeOffset();
        emit(OP_FOR_ITER, 0);
        endSlot = bytecodeOffset() - 1;
        if (!compileTarget(gen.target.get(), TargetCtx::Store)) return false;
    }
    
    std::vector<int> ifSlots;
    for (const auto& condition : gen.ifs) {
        if (!compileNode(condition.get())) return false;
        emit(OP_POP_JUMP_IF_FALSE, 0);
        ifSlots.push_back(bytecodeOffset() - 1);
    }
    
    if (!compileComprehension(generators, index + 1, innerBody)) return false;
    
    emit(OP_JUMP_ABSOLUTE, loopStart * 2);
    int loopEnd = bytecodeOffset();
    addPatch(endSlot, loopEnd);
    for (int slot : ifSlots) {
        addPatch(slot, loopStart);
    }
    return true;
}

bool Compiler::compileImport(ImportNode* n) {
    // Load __import__ via LOAD_GLOBAL (not LOAD_NAME) so it works in frame-free fast-path
    // function contexts where frame==nullptr would cause LOAD_NAME to silently push None.
    int idxImport = addName("__import__");
    emit(OP_LOAD_GLOBAL, (idxImport << 1) | 1);
    // Load module name string
    int idxMod = addConstant(PythonEnvironment::getInternedString(ctx_, n->moduleName.c_str())->asObject(ctx_));
    emit(OP_LOAD_CONST, idxMod);
    
    if (n->isAs) {
        // Pass True to return the leaf module
        int idxTrue = addConstant(PROTO_TRUE);
        emit(OP_LOAD_CONST, idxTrue);
        emit(OP_CALL_FUNCTION, 2);
    } else {
        emit(OP_CALL_FUNCTION, 1);
    }
    
    // Store in alias
    return emitNameOp(n->alias, TargetCtx::Store);
}

bool Compiler::compileImportFrom(ImportFromNode* n) {
    // Load __import__ via LOAD_GLOBAL so it works in frame-free fast-path function contexts.
    int idxImport = addName("__import__");
    emit(OP_LOAD_GLOBAL, (idxImport << 1) | 1);
    
    // name
    int idxMod = addConstant(PythonEnvironment::getInternedString(ctx_, n->moduleName.c_str())->asObject(ctx_));
    emit(OP_LOAD_CONST, idxMod);
    
    // globals (None for now or actual globals object)
    emit(OP_LOAD_CONST, addConstant(PROTO_NONE));
    // locals (None)
    emit(OP_LOAD_CONST, addConstant(PROTO_NONE));
    
    // fromlist: list of names
    std::vector<const proto::ProtoObject*> fromNames;
    for (auto& p : n->names) {
        fromNames.push_back(PythonEnvironment::getInternedString(ctx_, p.first.c_str())->asObject(ctx_));
    }
    const proto::ProtoList* fromList = ctx_->newList();
    for (auto* s : fromNames) fromList = fromList->appendLast(ctx_, s);
    emit(OP_LOAD_CONST, addConstant(fromList->asObject(ctx_)));
    
    // level
    emit(OP_LOAD_CONST, addConstant(ctx_->fromInteger(n->level)));
    
    emit(OP_CALL_FUNCTION, 5);
    
    if (n->names.size() == 1 && n->names[0].first == "*") {
        emit(OP_IMPORT_STAR, 0);
        return true;
    }
    
    // Stack has module object.
    for (auto& p : n->names) {
        // OP_IMPORT_FROM keeps module on stack and pushes attribute
        int idx = addName(p.first);
        emit(OP_IMPORT_FROM, (idx << 1));
        std::string alias = p.second.empty() ? p.first : p.second;
        emitNameOp(alias, TargetCtx::Store);
    }
    
    emit(OP_POP_TOP, 0);
    return true;
}

bool Compiler::compileTry(TryNode* n) {
    if (!n || !n->body) return false;
    
    // Setup exception handler
    emit(OP_SETUP_FINALLY, 0);
    int setupFinallySlot = bytecodeOffset() - 1;
    
    blockEnvStack_.push_back({BlockType::TryFinally, n->finalbody.get()});
    
    if (!compileNode(n->body.get())) return false;
    
    blockEnvStack_.pop_back();
    
    // No exception: pop the block and jump over handlers
    emit(OP_POP_BLOCK, 0);
    emit(OP_JUMP_ABSOLUTE, 0); 
    int jumpToPostHandlersSlot = bytecodeOffset() - 1;
    
    // Exception handler starts here
    addPatch(setupFinallySlot, bytecodeOffset());
    
    std::vector<int> jumpToEndLocations;
    if (!n->handlers.empty()) {
        for (auto& h : n->handlers) {
            int nextHandlerSlot = -1;
            if (h.type) {
                if (!compileNode(h.type.get())) return false;
                emit(OP_EXCEPTION_MATCH);
                nextHandlerSlot = bytecodeOffset();
                emit(OP_POP_JUMP_IF_FALSE, 0);
            }

            // Bind exception to name if present
            // In ProtoPython execution engine, the top of the stack is now Exception (Type), Exception (Value), Traceback (None)
            // Wait: since we changed OP_EXCEPTION_MATCH to pop ONLY `type`, `Type`, `Value`, `Traceback` are still on the stack.
            // CPython OP_EXCEPTION_MATCH pops the matching type but NOT the exception itself. 
            // We need to pop 3 items eventually, but first let's store it if needed.
            // Top of stack is `exc` (type). Next is `exc` (value). Next is `traceback` (none).
            if (!h.name.empty()) {
                emit(OP_DUP_TOP); // duplicates `exc` (type)
                if (!emitNameOp(h.name, TargetCtx::Store)) return false;
            }

            // Pop the 3 exception items from the stack since we're handling it
            emit(OP_POP_TOP, 0); // type
            emit(OP_POP_TOP, 0); // value
            emit(OP_POP_TOP, 0); // traceback
            
            if (!compileNode(h.body.get())) return false;
            
            emit(OP_POP_EXCEPT);

            int endJumpSlot = bytecodeOffset();
            emit(OP_JUMP_ABSOLUTE, 0); 
            jumpToEndLocations.push_back(endJumpSlot);

            if (nextHandlerSlot != -1) {
                addPatch(nextHandlerSlot, bytecodeOffset());
            }
        }
        // If we fall through all handlers, we still need to pop the exception info
        // wait, OP_RAISE_VARARGS 0 re-raises the active exception. It doesn't use the stack!
        // but the stack has 3 items left over! We need to pop them.
        emit(OP_POP_TOP, 0);
        emit(OP_POP_TOP, 0);
        emit(OP_POP_TOP, 0);
        emit(OP_RAISE_VARARGS, 0);
    } else {
        // PB5: try/finally without except handlers — pop the 3
        // exception items, run the `finally` body so its side-effects
        // happen even when an exception is propagating, then re-raise.
        emit(OP_POP_TOP, 0);
        emit(OP_POP_TOP, 0);
        emit(OP_POP_TOP, 0);
        if (n->finalbody) {
            if (!compileNode(n->finalbody.get())) return false;
        }
        emit(OP_RAISE_VARARGS, 0);
    }

    // Successful try jumps here (to Else block)
    addPatch(jumpToPostHandlersSlot, bytecodeOffset());

    if (n->orelse) {
        if (!compileNode(n->orelse.get())) return false;
    }

    // Handled exceptions (from except blocks) jump here (after Else block)
    int postElseLabel = bytecodeOffset();
    for (int locSlot : jumpToEndLocations) {
        addPatch(locSlot, postElseLabel);
    }

    if (n->finalbody) {
        if (!compileNode(n->finalbody.get())) return false;
    }
    return true;
}

bool Compiler::compileRaise(RaiseNode* n) {
    if (n->exc) {
        if (!compileNode(n->exc.get())) return false;
        emit(OP_RAISE_VARARGS, 1);
    } else {
        emit(OP_RAISE_VARARGS, 0); // re-raise
    }
    return true;
}

bool Compiler::compileWith(WithNode* n) {
    if (!n) return false;
    return compileWithItems(n->items, 0, n->body.get());
}

bool Compiler::compileWithItems(const std::vector<WithItem>& items, size_t index, ASTNode* body) {
    if (index == items.size()) {
        return compileNode(body);
    }
    
    const auto& item = items[index];
    if (!compileNode(item.context_expr.get())) return false;
    
    emit(OP_SETUP_WITH, 0); // Handler to be patched
    int setupSlot = bytecodeOffset() - 1;
    
    blockEnvStack_.push_back({BlockType::With, nullptr});
    
    if (item.optional_vars) {
        if (!compileTarget(item.optional_vars.get(), TargetCtx::Store)) return false;
    } else {
        emit(OP_POP_TOP);
    }
    
    if (!compileWithItems(items, index + 1, body)) return false;
    
    blockEnvStack_.pop_back();
    
    // Normal exit
    emit(OP_POP_BLOCK);
    int noneIdx = addConstant(PROTO_NONE);
    emit(OP_LOAD_CONST, noneIdx);
    emit(OP_WITH_CLEANUP);
    emit(OP_POP_TOP); // pop suppression flag
    
    emit(OP_JUMP_ABSOLUTE, 0); // Jump over cleanup
    int jumpOverCleanupSlot = bytecodeOffset() - 1;
    
    // Cleanup/Exception exit handler
    int cleanupLabel = bytecodeOffset();
    addPatch(setupSlot, cleanupLabel);
    emit(OP_WITH_CLEANUP);
    emit(OP_POP_JUMP_IF_TRUE, 0); // if suppressed, jump to done
    int suppressedJumpSlot = bytecodeOffset() - 1;
    emit(OP_RAISE_VARARGS, 0);
    
    int postWithLabel = bytecodeOffset();
    addPatch(jumpOverCleanupSlot, postWithLabel);
    addPatch(suppressedJumpSlot, postWithLabel);
    
    return true;
}

bool Compiler::compileAugAssign(AugAssignNode* n) {
    if (!n || !n->target) return false;

    if (auto* sub = dynamic_cast<SubscriptNode*>(n->target.get())) {
        // 1. Compile value (base) and index
        if (!compileNode(sub->value.get())) return false;
        if (!compileNode(sub->index.get())) return false;
        // 2. Duplicate them for later storage
        emit(OP_DUP_TOP_TWO, 0);
        // 3. Load currently stored value
        emit(OP_BINARY_SUBSCR, 0);
    } else {
        // Load old value
        if (!compileNode(n->target.get())) return false;
    }

    // Load value to add/sub/...
    if (!compileNode(n->value.get())) return false;

    // Perform operation
    int op = OP_INPLACE_ADD;
    switch (n->op) {
        case TokenType::PlusAssign: op = OP_INPLACE_ADD; break;
        case TokenType::MinusAssign: op = OP_INPLACE_SUBTRACT; break;
        case TokenType::StarAssign: op = OP_INPLACE_MULTIPLY; break;
        case TokenType::SlashAssign: op = OP_INPLACE_TRUE_DIVIDE; break;
        case TokenType::ModuloAssign: op = OP_INPLACE_MODULO; break;
        case TokenType::AndAssign: op = OP_INPLACE_AND; break;
        case TokenType::OrAssign: op = OP_INPLACE_OR; break;
        case TokenType::XorAssign: op = OP_INPLACE_XOR; break;
        case TokenType::LShiftAssign: op = OP_INPLACE_LSHIFT; break;
        case TokenType::RShiftAssign: op = OP_INPLACE_RSHIFT; break;
        case TokenType::DoubleStarAssign: op = OP_INPLACE_POWER; break;
        case TokenType::DoubleSlashAssign: op = OP_INPLACE_FLOOR_DIVIDE; break;
        case TokenType::AtAssign: op = OP_INPLACE_MATRIX_MULTIPLY; break;
        default: return false;
    }
    emit(op, 0);

    // Store back
    if (auto* name = dynamic_cast<NameNode*>(n->target.get())) {
        return emitNameOp(name->id, TargetCtx::Store);
    } else if (auto* att = dynamic_cast<AttributeNode*>(n->target.get())) {
        if (!compileNode(att->value.get())) return false;
        int nameIdx = addName(att->attr);
        emit(OP_STORE_ATTR, (nameIdx << 1));
        return true;
    } else if (auto* sub = dynamic_cast<SubscriptNode*>(n->target.get())) {
        emit(OP_STORE_SUBSCR, 0);
        return true;
    }
    // TODO: support subscript aug assign
    return false;
}

bool Compiler::statementLeavesValue(ASTNode* node) {
    if (!node) return false;
    if (dynamic_cast<AssignNode*>(node) || dynamic_cast<AugAssignNode*>(node) || dynamic_cast<ForNode*>(node) ||
        dynamic_cast<AsyncForNode*>(node) ||
        dynamic_cast<IfNode*>(node) || dynamic_cast<FunctionDefNode*>(node) ||
        dynamic_cast<ClassDefNode*>(node) || dynamic_cast<WhileNode*>(node) ||
        dynamic_cast<ImportNode*>(node) || dynamic_cast<GlobalNode*>(node) ||
        dynamic_cast<ReturnNode*>(node) || dynamic_cast<DeleteNode*>(node) ||
        dynamic_cast<TryNode*>(node) || dynamic_cast<WithNode*>(node) ||
        dynamic_cast<AssertNode*>(node) || dynamic_cast<PassNode*>(node) ||
        dynamic_cast<BreakNode*>(node) || dynamic_cast<ContinueNode*>(node)) return false;
    return true;
}

static void collectNonlocalsFromNode(ASTNode* node, std::unordered_set<std::string>& out) {
    if (!node) return;
    if (auto* n = dynamic_cast<NonlocalNode*>(node)) {
        for (const auto& name : n->names) out.insert(name);
        return;
    }
    if (auto* a = dynamic_cast<AssignNode*>(node)) {
        for (auto& t : a->targets) collectNonlocalsFromNode(t.get(), out);
        collectNonlocalsFromNode(a->value.get(), out);
        return;
    }
    if (auto* s = dynamic_cast<SuiteNode*>(node)) {
        for (auto& st : s->statements) collectNonlocalsFromNode(st.get(), out);
        return;
    }
    if (auto* f = dynamic_cast<ForNode*>(node)) {
        collectNonlocalsFromNode(f->target.get(), out);
        collectNonlocalsFromNode(f->iter.get(), out);
        collectNonlocalsFromNode(f->body.get(), out);
        return;
    }
    if (auto* af = dynamic_cast<AsyncForNode*>(node)) {
        collectNonlocalsFromNode(af->target.get(), out);
        collectNonlocalsFromNode(af->iter.get(), out);
        collectNonlocalsFromNode(af->body.get(), out);
        if (af->orelse) collectNonlocalsFromNode(af->orelse.get(), out);
        return;
    }
    if (auto* iff = dynamic_cast<IfNode*>(node)) {
        collectNonlocalsFromNode(iff->test.get(), out);
        collectNonlocalsFromNode(iff->body.get(), out);
        if (iff->orelse) collectNonlocalsFromNode(iff->orelse.get(), out);
        return;
    }
    if (auto* tr = dynamic_cast<TryNode*>(node)) {
        collectNonlocalsFromNode(tr->body.get(), out);
        for (auto& h : tr->handlers) collectNonlocalsFromNode(h.body.get(), out);
        if (tr->finalbody) collectNonlocalsFromNode(tr->finalbody.get(), out);
        return;
    }
    if (auto* w = dynamic_cast<WithNode*>(node)) {
        collectNonlocalsFromNode(w->body.get(), out);
        return;
    }
    if (auto* aw = dynamic_cast<AsyncWithNode*>(node)) {
        collectNonlocalsFromNode(aw->body.get(), out);
        return;
    }
    if (auto* wh = dynamic_cast<WhileNode*>(node)) {
        collectNonlocalsFromNode(wh->test.get(), out);
        collectNonlocalsFromNode(wh->body.get(), out);
        if (wh->orelse) collectNonlocalsFromNode(wh->orelse.get(), out);
        return;
    }
}

static void collectGlobalsFromNode(ASTNode* node, std::unordered_set<std::string>& globalsOut) {
    if (!node) return;
    if (auto* g = dynamic_cast<GlobalNode*>(node)) {
        for (const auto& name : g->names) globalsOut.insert(name);
        return;
    }
    if (auto* a = dynamic_cast<AssignNode*>(node)) {
        for (auto& t : a->targets) collectGlobalsFromNode(t.get(), globalsOut);
        collectGlobalsFromNode(a->value.get(), globalsOut);
        return;
    }
    if (auto* s = dynamic_cast<SuiteNode*>(node)) {
        for (auto& st : s->statements) collectGlobalsFromNode(st.get(), globalsOut);
        return;
    }
    if (auto* f = dynamic_cast<ForNode*>(node)) {
        collectGlobalsFromNode(f->target.get(), globalsOut);
        collectGlobalsFromNode(f->iter.get(), globalsOut);
        collectGlobalsFromNode(f->body.get(), globalsOut);
        return;
    }
    if (auto* af = dynamic_cast<AsyncForNode*>(node)) {
        collectGlobalsFromNode(af->target.get(), globalsOut);
        collectGlobalsFromNode(af->iter.get(), globalsOut);
        collectGlobalsFromNode(af->body.get(), globalsOut);
        if (af->orelse) collectGlobalsFromNode(af->orelse.get(), globalsOut);
        return;
    }
    if (auto* iff = dynamic_cast<IfNode*>(node)) {
        collectGlobalsFromNode(iff->test.get(), globalsOut);
        collectGlobalsFromNode(iff->body.get(), globalsOut);
        if (iff->orelse) collectGlobalsFromNode(iff->orelse.get(), globalsOut);
        return;
    }
    if (auto* tr = dynamic_cast<TryNode*>(node)) {
        collectGlobalsFromNode(tr->body.get(), globalsOut);
        for (auto& h : tr->handlers) {
            if (h.type) collectGlobalsFromNode(h.type.get(), globalsOut);
            collectGlobalsFromNode(h.body.get(), globalsOut);
        }
        if (tr->orelse) collectGlobalsFromNode(tr->orelse.get(), globalsOut);
        if (tr->finalbody) collectGlobalsFromNode(tr->finalbody.get(), globalsOut);
        return;
    }
    if (auto* r = dynamic_cast<RaiseNode*>(node)) {
        if (r->exc) collectGlobalsFromNode(r->exc.get(), globalsOut);
        if (r->cause) collectGlobalsFromNode(r->cause.get(), globalsOut);
        return;
    }
    if (auto* w = dynamic_cast<WithNode*>(node)) {
        for (auto& item : w->items) {
            collectGlobalsFromNode(item.context_expr.get(), globalsOut);
            if (item.optional_vars) collectGlobalsFromNode(item.optional_vars.get(), globalsOut);
        }
        collectGlobalsFromNode(w->body.get(), globalsOut);
        return;
    }
    if (auto* aw = dynamic_cast<AsyncWithNode*>(node)) {
        for (auto& item : aw->items) {
            collectGlobalsFromNode(item.context_expr.get(), globalsOut);
            if (item.optional_vars) collectGlobalsFromNode(item.optional_vars.get(), globalsOut);
        }
        collectGlobalsFromNode(aw->body.get(), globalsOut);
        return;
    }
    if (auto* c = dynamic_cast<CallNode*>(node)) {
        collectGlobalsFromNode(c->func.get(), globalsOut);
        for (auto& arg : c->args) collectGlobalsFromNode(arg.get(), globalsOut);
        return;
    }
    if (auto* att = dynamic_cast<AttributeNode*>(node)) {
        collectGlobalsFromNode(att->value.get(), globalsOut);
        return;
    }
    if (auto* sub = dynamic_cast<SubscriptNode*>(node)) {
        collectGlobalsFromNode(sub->value.get(), globalsOut);
        collectGlobalsFromNode(sub->index.get(), globalsOut);
        return;
    }
    if (auto* sl = dynamic_cast<SliceNode*>(node)) {
        if (sl->start) collectGlobalsFromNode(sl->start.get(), globalsOut);
        if (sl->stop) collectGlobalsFromNode(sl->stop.get(), globalsOut);
        if (sl->step) collectGlobalsFromNode(sl->step.get(), globalsOut);
        return;
    }
    if (auto* b = dynamic_cast<BinOpNode*>(node)) {
        collectGlobalsFromNode(b->left.get(), globalsOut);
        collectGlobalsFromNode(b->right.get(), globalsOut);
        return;
    }
    if (auto* u = dynamic_cast<UnaryOpNode*>(node)) {
        collectGlobalsFromNode(u->operand.get(), globalsOut);
        return;
    }
    if (auto* lst = dynamic_cast<ListLiteralNode*>(node)) {
        for (auto& e : lst->elements) collectGlobalsFromNode(e.get(), globalsOut);
        return;
    }
    if (auto* d = dynamic_cast<DictLiteralNode*>(node)) {
        for (size_t i = 0; i < d->keys.size(); ++i) {
            collectGlobalsFromNode(d->keys[i].get(), globalsOut);
            collectGlobalsFromNode(d->values[i].get(), globalsOut);
        }
        return;
    }
    if (auto* tup = dynamic_cast<TupleLiteralNode*>(node)) {
        for (auto& e : tup->elements) collectGlobalsFromNode(e.get(), globalsOut);
        return;
    }
}

void Compiler::collectLocalsFromBody(ASTNode* body,
    std::unordered_set<std::string>& globalsOut,
    std::unordered_set<std::string>& nonlocalsOut,
    std::vector<std::string>& localsOrdered) {
    globalsOut.clear();
    nonlocalsOut.clear();
    localsOrdered.clear();
    
    // 1. First pass: find all 'global' and 'nonlocal' declarations
    collectGlobalsFromNode(body, globalsOut);
    collectNonlocalsFromNode(body, nonlocalsOut);
    
    // 2. Second pass: find all names that are assigned/stored (implicitly local)
    std::unordered_set<std::string> defined;
    collectDefinedNames(body, defined);
    
    // Filter out those explicitly declared global
    std::unordered_set<std::string> seen;
    // We want a stable order, so ideally we'd traverse the AST once more 
    // to see the order of appearance of these 'defined' names that are NOT global.
    // However, collectDefinedNames doesn't give us order.
    // For now, let's just add them.
    for (const auto& name : defined) {
        if (globalsOut.find(name) == globalsOut.end() && nonlocalsOut.find(name) == nonlocalsOut.end()) {
            if (seen.find(name) == seen.end()) {
                localsOrdered.push_back(name);
                seen.insert(name);
            }
        }
    }
}

/** Collect names used (read or written) in node into out. */
static void collectUsedNames(ASTNode* node, std::unordered_set<std::string>& out) {
    if (!node) return;
    if (auto* nm = dynamic_cast<NameNode*>(node)) {
        out.insert(nm->id);
        return;
    }
    if (auto* a = dynamic_cast<AssignNode*>(node)) {
        for (auto& t : a->targets) collectUsedNames(t.get(), out);
        collectUsedNames(a->value.get(), out);
        return;
    }
    if (auto* s = dynamic_cast<SuiteNode*>(node)) {
        for (auto& st : s->statements) collectUsedNames(st.get(), out);
        return;
    }
    if (auto* f = dynamic_cast<ForNode*>(node)) {
        collectUsedNames(f->target.get(), out);
        collectUsedNames(f->iter.get(), out);
        collectUsedNames(f->body.get(), out);
        return;
    }
    if (auto* af = dynamic_cast<AsyncForNode*>(node)) {
        collectUsedNames(af->target.get(), out);
        collectUsedNames(af->iter.get(), out);
        collectUsedNames(af->body.get(), out);
        if (af->orelse) collectUsedNames(af->orelse.get(), out);
        return;
    }
    if (auto* iff = dynamic_cast<IfNode*>(node)) {
        collectUsedNames(iff->test.get(), out);
        collectUsedNames(iff->body.get(), out);
        if (iff->orelse) collectUsedNames(iff->orelse.get(), out);
        return;
    }
    if (auto* fn = dynamic_cast<FunctionDefNode*>(node)) {
        // PI: include decorators and default-argument expressions in
        // the used-names set; these are evaluated in the enclosing
        // scope (not the function body), so closure capture must see
        // them.  Without this, e.g. `@abstractmethod` inside a class
        // body fails with NameError when the decorator was imported
        // in the enclosing function.
        for (auto& d : fn->decorator_list) collectUsedNames(d.get(), out);
        for (auto& d : fn->defaults) collectUsedNames(d.get(), out);
        for (auto& d : fn->kw_defaults) {
            if (d) collectUsedNames(d.get(), out);
        }
        for (const auto& p : fn->parameters) out.insert(p);
        collectUsedNames(fn->body.get(), out);
        return;
    }
    if (auto* cd = dynamic_cast<ClassDefNode*>(node)) {
        // PI: a class definition's bases + decorators + class-level
        // keywords are evaluated in the enclosing scope.
        for (auto& d : cd->decorator_list) collectUsedNames(d.get(), out);
        for (auto& b : cd->bases) collectUsedNames(b.get(), out);
        for (auto& kw : cd->keywords) {
            if (kw.second) collectUsedNames(kw.second.get(), out);
        }
        return;
    }
    if (auto* afn = dynamic_cast<AsyncFunctionDefNode*>(node)) {
        for (auto& d : afn->decorator_list) collectUsedNames(d.get(), out);
        for (auto& d : afn->defaults) collectUsedNames(d.get(), out);
        for (auto& d : afn->kw_defaults) {
            if (d) collectUsedNames(d.get(), out);
        }
        for (const auto& p : afn->parameters) out.insert(p);
        collectUsedNames(afn->body.get(), out);
        return;
    }
    if (auto* c = dynamic_cast<CallNode*>(node)) {
        collectUsedNames(c->func.get(), out);
        for (auto& arg : c->args) collectUsedNames(arg.get(), out);
        for (auto& kw : c->keywords) {
            if (kw.second) collectUsedNames(kw.second.get(), out);
        }
        return;
    }
    if (auto* att = dynamic_cast<AttributeNode*>(node)) {
        collectUsedNames(att->value.get(), out);
        return;
    }
    if (auto* sub = dynamic_cast<SubscriptNode*>(node)) {
        collectUsedNames(sub->value.get(), out);
        collectUsedNames(sub->index.get(), out);
        return;
    }
    if (auto* b = dynamic_cast<BinOpNode*>(node)) {
        collectUsedNames(b->left.get(), out);
        collectUsedNames(b->right.get(), out);
        return;
    }
    if (auto* u = dynamic_cast<UnaryOpNode*>(node)) {
        collectUsedNames(u->operand.get(), out);
        return;
    }
    if (auto* ret = dynamic_cast<ReturnNode*>(node)) {
        collectUsedNames(ret->value.get(), out);
        return;
    }
    if (auto* y = dynamic_cast<YieldNode*>(node)) {
        collectUsedNames(y->value.get(), out);
        return;
    }
    if (auto* t = dynamic_cast<TryNode*>(node)) {
        collectUsedNames(t->body.get(), out);
        for (auto& h : t->handlers) {
            collectUsedNames(h.body.get(), out);
            collectUsedNames(h.type.get(), out);
        }
        if (t->orelse) collectUsedNames(t->orelse.get(), out);
        if (t->finalbody) collectUsedNames(t->finalbody.get(), out);
    }
    if (auto* w = dynamic_cast<WhileNode*>(node)) {
        collectUsedNames(w->test.get(), out);
        collectUsedNames(w->body.get(), out);
        if (w->orelse) collectUsedNames(w->orelse.get(), out);
        return;
    }
    if (auto* cond = dynamic_cast<ConditionalExprNode*>(node)) {
        collectUsedNames(cond->body.get(), out);
        collectUsedNames(cond->test.get(), out);
        collectUsedNames(cond->orelse.get(), out);
        return;
    }
    if (auto* starred = dynamic_cast<StarredNode*>(node)) {
        collectUsedNames(starred->value.get(), out);
        return;
    }
    if (auto* named = dynamic_cast<NamedExprNode*>(node)) {
        collectUsedNames(named->value.get(), out);
        collectUsedNames(named->target.get(), out);
        return;
    }
    if (auto* lst = dynamic_cast<ListLiteralNode*>(node)) {
        for (auto& e : lst->elements) collectUsedNames(e.get(), out);
        return;
    }
    if (auto* tup = dynamic_cast<TupleLiteralNode*>(node)) {
        for (auto& e : tup->elements) collectUsedNames(e.get(), out);
        return;
    }
    if (auto* d = dynamic_cast<DictLiteralNode*>(node)) {
        for (size_t i = 0; i < d->keys.size(); ++i) {
            collectUsedNames(d->keys[i].get(), out);
            collectUsedNames(d->values[i].get(), out);
        }
        return;
    }
    if (auto* lam = dynamic_cast<LambdaNode*>(node)) {
        for (const auto& p : lam->parameters) out.insert(p);
        collectUsedNames(lam->body.get(), out);
        return;
    }
    if (auto* lc = dynamic_cast<ListCompNode*>(node)) {
        for (const auto& g : lc->generators) {
            collectUsedNames(g.target.get(), out);
            collectUsedNames(g.iter.get(), out);
            for (auto& cond : g.ifs) collectUsedNames(cond.get(), out);
        }
        collectUsedNames(lc->elt.get(), out);
        return;
    }
    if (auto* dc = dynamic_cast<DictCompNode*>(node)) {
        for (const auto& g : dc->generators) {
            collectUsedNames(g.target.get(), out);
            collectUsedNames(g.iter.get(), out);
            for (auto& cond : g.ifs) collectUsedNames(cond.get(), out);
        }
        collectUsedNames(dc->key.get(), out);
        collectUsedNames(dc->value.get(), out);
        return;
    }
    if (auto* sc = dynamic_cast<SetCompNode*>(node)) {
        for (const auto& g : sc->generators) {
            collectUsedNames(g.target.get(), out);
            collectUsedNames(g.iter.get(), out);
            for (auto& cond : g.ifs) collectUsedNames(cond.get(), out);
        }
        collectUsedNames(sc->elt.get(), out);
        return;
    }
    if (auto* ge = dynamic_cast<GeneratorExpNode*>(node)) {
        for (const auto& g : ge->generators) {
            collectUsedNames(g.target.get(), out);
            collectUsedNames(g.iter.get(), out);
            for (auto& cond : g.ifs) collectUsedNames(cond.get(), out);
        }
        collectUsedNames(ge->elt.get(), out);
        return;
    }
    if (auto* slice = dynamic_cast<SliceNode*>(node)) {
        if (slice->start) collectUsedNames(slice->start.get(), out);
        if (slice->stop) collectUsedNames(slice->stop.get(), out);
        if (slice->step) collectUsedNames(slice->step.get(), out);
        return;
    }
    if (auto* set = dynamic_cast<SetLiteralNode*>(node)) {
        for (auto& e : set->elements) collectUsedNames(e.get(), out);
        return;
    }
    if (auto* as = dynamic_cast<AssertNode*>(node)) {
        collectUsedNames(as->test.get(), out);
        if (as->msg) collectUsedNames(as->msg.get(), out);
        return;
    }
    if (auto* w = dynamic_cast<WithNode*>(node)) {
        for (auto& item : w->items) {
            collectUsedNames(item.context_expr.get(), out);
            if (item.optional_vars) collectUsedNames(item.optional_vars.get(), out);
        }
        collectUsedNames(w->body.get(), out);
        return;
    }
    if (auto* aw = dynamic_cast<AsyncWithNode*>(node)) {
        for (auto& item : aw->items) {
            collectUsedNames(item.context_expr.get(), out);
            if (item.optional_vars) collectUsedNames(item.optional_vars.get(), out);
        }
        collectUsedNames(aw->body.get(), out);
        return;
    }
    if (auto* d = dynamic_cast<DeleteNode*>(node)) {
        for (auto& target : d->targets) collectUsedNames(target.get(), out);
        return;
    }
    if (auto* r = dynamic_cast<RaiseNode*>(node)) {
        if (r->exc) collectUsedNames(r->exc.get(), out);
        if (r->cause) collectUsedNames(r->cause.get(), out);
        return;
    }
    if (auto* y = dynamic_cast<YieldNode*>(node)) {
        if (y->value) collectUsedNames(y->value.get(), out);
        return;
    }
    if (auto* aw = dynamic_cast<AwaitNode*>(node)) {
        collectUsedNames(aw->value.get(), out);
        return;
    }
    if (auto* js = dynamic_cast<JoinedStrNode*>(node)) {
        for (auto& v : js->values) collectUsedNames(v.get(), out);
        return;
    }
    if (auto* fv = dynamic_cast<FormattedValueNode*>(node)) {
        collectUsedNames(fv->value.get(), out);
        return;
    }
    if (auto* s = dynamic_cast<SuiteNode*>(node)) {
        for (auto& st : s->statements) collectUsedNames(st.get(), out);
        return;
    }
}

/** Collect names defined in node (assigned or are params of a nested def). */
static void collectDefinedNames(ASTNode* node, std::unordered_set<std::string>& out) {
    if (!node) return;
    if (auto* a = dynamic_cast<AssignNode*>(node)) {
        for (auto& t : a->targets) collectDefinedNames(t.get(), out);
        return;
    }
    if (auto* aa = dynamic_cast<AnnAssignNode*>(node)) {
        // Per PEP 526 / CPython: an annotated target at function scope
        // — even without an initial value (`x: int`) — binds the name as
        // a local.  Accessing it before assignment must raise
        // UnboundLocalError, not NameError.
        // Exception: `(name): T` (parenthesized target) does NOT declare
        // a local; the annotation expression is evaluated for side
        // effects but the name behaves as if no annotation existed.
        if (auto* nm = dynamic_cast<NameNode*>(aa->target.get())) {
            if (!nm->parenthesized) out.insert(nm->id);
        }
        return;
    }
    if (auto* named = dynamic_cast<NamedExprNode*>(node)) {
        collectDefinedNames(named->target.get(), out);
        return;
    }
    if (auto* nm = dynamic_cast<NameNode*>(node)) {
        out.insert(nm->id);
        return;
    }
    if (auto* s = dynamic_cast<SuiteNode*>(node)) {
        for (auto& st : s->statements) collectDefinedNames(st.get(), out);
        return;
    }
    if (auto* f = dynamic_cast<ForNode*>(node)) {
        collectDefinedNames(f->target.get(), out);
        collectDefinedNames(f->body.get(), out);
        return;
    }
    if (auto* af = dynamic_cast<AsyncForNode*>(node)) {
        collectDefinedNames(af->target.get(), out);
        collectDefinedNames(af->body.get(), out);
        if (af->orelse) collectDefinedNames(af->orelse.get(), out);
        return;
    }
    if (auto* iff = dynamic_cast<IfNode*>(node)) {
        collectDefinedNames(iff->body.get(), out);
        if (iff->orelse) collectDefinedNames(iff->orelse.get(), out);
        return;
    }
    if (auto* fn = dynamic_cast<FunctionDefNode*>(node)) {
        // A function definition defines the function name in the CURRENT scope.
        // It does NOT define its parameters or internal names in the outer scope.
        out.insert(fn->name);
        return;
    }
    if (auto* cl = dynamic_cast<ClassDefNode*>(node)) {
        // A class definition defines the class name in the CURRENT scope.
        out.insert(cl->name);
        return;
    }
    if (auto* imp = dynamic_cast<ImportNode*>(node)) {
        if (imp->isAs) {
            out.insert(imp->alias);
        } else {
            std::string root = imp->moduleName;
            size_t dot = root.find('.');
            if (dot != std::string::npos) root = root.substr(0, dot);
            out.insert(root);
        }
        return;
    }
    if (auto* ifrom = dynamic_cast<ImportFromNode*>(node)) {
        for (const auto& item : ifrom->names) {
            if (!item.second.empty()) out.insert(item.second);
            else out.insert(item.first);
        }
        return;
    }
    if (auto* t = dynamic_cast<TryNode*>(node)) {
        collectDefinedNames(t->body.get(), out);
        for (auto& h : t->handlers) {
            if (h.name != "") out.insert(h.name);
            collectDefinedNames(h.body.get(), out);
        }
        if (t->orelse) collectDefinedNames(t->orelse.get(), out);
        if (t->finalbody) collectDefinedNames(t->finalbody.get(), out);
        return;
    }
    if (auto* w = dynamic_cast<WithNode*>(node)) {
        for (auto& item : w->items) {
            if (item.optional_vars) collectDefinedNames(item.optional_vars.get(), out);
        }
        collectDefinedNames(w->body.get(), out);
        return;
    }
    if (auto* aw = dynamic_cast<AsyncWithNode*>(node)) {
        for (auto& item : aw->items) {
            if (item.optional_vars) collectDefinedNames(item.optional_vars.get(), out);
        }
        collectDefinedNames(aw->body.get(), out);
        return;
    }
    if (auto* nm = dynamic_cast<NameNode*>(node)) {
        // NameNode in Store context (part of a target list)
        out.insert(nm->id);
        return;
    }
    if (auto* tup = dynamic_cast<TupleLiteralNode*>(node)) {
        for (auto& e : tup->elements) collectDefinedNames(e.get(), out);
        return;
    }
    if (auto* lst = dynamic_cast<ListLiteralNode*>(node)) {
        for (auto& e : lst->elements) collectDefinedNames(e.get(), out);
        return;
    }
}

// Walk a function body and collect the set of free variables of every
// nested function-like scope (def, async def, lambda, list/set/dict
// comprehension, generator expression).  These are names that the
// nested scope reads but does not itself define; if any of them match
// the outer's own locals or parameters, the outer must store its
// locals on the frame (forceMapped) so the nested scope's
// closure-frame parent walk reads the live value rather than a stale
// snapshot taken at BUILD_FUNCTION time.
//
// This walker is deliberately strict — it only descends into nested
// scope-creating nodes, never into bare references — so the result is
// exactly "names captured by nested scopes from somewhere outside
// themselves".  Globals are excluded by checking that the name is not
// already in `globalsHere`; intrinsic names (parameters of the nested
// scope, names assigned within it) are excluded by the inner
// defined/used scan.
static void collectNestedScopeFreeVarsImpl(ASTNode* node, std::unordered_set<std::string>& out);

static void scopeFreeVars(ASTNode* body,
                          const std::unordered_set<std::string>& defined,
                          std::unordered_set<std::string>& out) {
    if (!body) return;
    std::unordered_set<std::string> used;
    collectUsedNames(body, used);
    for (const auto& name : used) {
        if (defined.count(name)) continue;
        out.insert(name);
    }
    // Recurse into nested scopes inside `body` so deeper levels also
    // contribute their free variables (they bubble up through this
    // outer scope if not bound here).
    collectNestedScopeFreeVarsImpl(body, out);
}

static void collectNestedScopeFreeVarsImpl(ASTNode* node, std::unordered_set<std::string>& out) {
    if (!node) return;
    if (auto* fn = dynamic_cast<FunctionDefNode*>(node)) {
        std::unordered_set<std::string> def;
        collectDefinedNames(fn->body.get(), def);
        for (const auto& p : fn->parameters) def.insert(p);
        for (const auto& kw : fn->kwonlyargs) def.insert(kw);
        if (!fn->vararg.empty()) def.insert(fn->vararg);
        if (!fn->kwarg.empty())  def.insert(fn->kwarg);
        scopeFreeVars(fn->body.get(), def, out);
        return;
    }
    if (auto* afn = dynamic_cast<AsyncFunctionDefNode*>(node)) {
        std::unordered_set<std::string> def;
        collectDefinedNames(afn->body.get(), def);
        for (const auto& p : afn->parameters) def.insert(p);
        for (const auto& kw : afn->kwonlyargs) def.insert(kw);
        if (!afn->vararg.empty()) def.insert(afn->vararg);
        if (!afn->kwarg.empty())  def.insert(afn->kwarg);
        scopeFreeVars(afn->body.get(), def, out);
        return;
    }
    if (auto* lam = dynamic_cast<LambdaNode*>(node)) {
        std::unordered_set<std::string> def;
        for (const auto& p : lam->parameters) def.insert(p);
        for (const auto& kw : lam->kwonlyargs) def.insert(kw);
        if (!lam->vararg.empty()) def.insert(lam->vararg);
        if (!lam->kwarg.empty())  def.insert(lam->kwarg);
        scopeFreeVars(lam->body.get(), def, out);
        return;
    }
    auto compHelper = [&](const std::vector<Comprehension>& gens, ASTNode* a, ASTNode* b) {
        std::unordered_set<std::string> def;
        for (const auto& g : gens) collectDefinedNames(g.target.get(), def);
        // Outermost iter is evaluated in the enclosing scope, but every
        // other position lives in the comp scope.  All non-defined
        // names in those positions are free vars of the comp scope.
        for (size_t i = 0; i < gens.size(); ++i) {
            std::unordered_set<std::string> used;
            // Treat the OUTERMOST iter as not-belonging to this comp scope
            // (the compiler evaluates it in the enclosing scope).  Its
            // free vars are picked up by the *outer* scope's analysis,
            // not ours.
            if (i > 0) collectUsedNames(gens[i].iter.get(), used);
            for (const auto& cond : gens[i].ifs) collectUsedNames(cond.get(), used);
            for (const auto& nm : used) if (!def.count(nm)) out.insert(nm);
        }
        if (a) {
            std::unordered_set<std::string> used;
            collectUsedNames(a, used);
            for (const auto& nm : used) if (!def.count(nm)) out.insert(nm);
        }
        if (b) {
            std::unordered_set<std::string> used;
            collectUsedNames(b, used);
            for (const auto& nm : used) if (!def.count(nm)) out.insert(nm);
        }
    };
    if (auto* lc = dynamic_cast<ListCompNode*>(node))      { compHelper(lc->generators, lc->elt.get(), nullptr); return; }
    if (auto* sc = dynamic_cast<SetCompNode*>(node))       { compHelper(sc->generators, sc->elt.get(), nullptr); return; }
    if (auto* dc = dynamic_cast<DictCompNode*>(node))      { compHelper(dc->generators, dc->key.get(), dc->value.get()); return; }
    if (auto* ge = dynamic_cast<GeneratorExpNode*>(node))  { compHelper(ge->generators, ge->elt.get(), nullptr); return; }

    // Generic recursive descent into compound statements.  We skip
    // class bodies — their locals are bound to the namespace dict, not
    // closed over by their own methods.
    if (auto* s = dynamic_cast<SuiteNode*>(node)) {
        for (auto& st : s->statements) collectNestedScopeFreeVarsImpl(st.get(), out);
        return;
    }
    if (auto* iff = dynamic_cast<IfNode*>(node)) {
        collectNestedScopeFreeVarsImpl(iff->test.get(), out);
        collectNestedScopeFreeVarsImpl(iff->body.get(), out);
        if (iff->orelse) collectNestedScopeFreeVarsImpl(iff->orelse.get(), out);
        return;
    }
    if (auto* w = dynamic_cast<WhileNode*>(node)) {
        collectNestedScopeFreeVarsImpl(w->test.get(), out);
        collectNestedScopeFreeVarsImpl(w->body.get(), out);
        if (w->orelse) collectNestedScopeFreeVarsImpl(w->orelse.get(), out);
        return;
    }
    if (auto* f = dynamic_cast<ForNode*>(node)) {
        collectNestedScopeFreeVarsImpl(f->iter.get(), out);
        collectNestedScopeFreeVarsImpl(f->body.get(), out);
        if (f->orelse) collectNestedScopeFreeVarsImpl(f->orelse.get(), out);
        return;
    }
    if (auto* af = dynamic_cast<AsyncForNode*>(node)) {
        collectNestedScopeFreeVarsImpl(af->iter.get(), out);
        collectNestedScopeFreeVarsImpl(af->body.get(), out);
        if (af->orelse) collectNestedScopeFreeVarsImpl(af->orelse.get(), out);
        return;
    }
    if (auto* tr = dynamic_cast<TryNode*>(node)) {
        collectNestedScopeFreeVarsImpl(tr->body.get(), out);
        for (auto& h : tr->handlers) collectNestedScopeFreeVarsImpl(h.body.get(), out);
        if (tr->orelse) collectNestedScopeFreeVarsImpl(tr->orelse.get(), out);
        if (tr->finalbody) collectNestedScopeFreeVarsImpl(tr->finalbody.get(), out);
        return;
    }
    if (auto* wn = dynamic_cast<WithNode*>(node)) {
        for (auto& it : wn->items) collectNestedScopeFreeVarsImpl(it.context_expr.get(), out);
        collectNestedScopeFreeVarsImpl(wn->body.get(), out);
        return;
    }
    if (auto* awn = dynamic_cast<AsyncWithNode*>(node)) {
        for (auto& it : awn->items) collectNestedScopeFreeVarsImpl(it.context_expr.get(), out);
        collectNestedScopeFreeVarsImpl(awn->body.get(), out);
        return;
    }
    if (auto* a = dynamic_cast<AssignNode*>(node)) {
        for (auto& t : a->targets) collectNestedScopeFreeVarsImpl(t.get(), out);
        collectNestedScopeFreeVarsImpl(a->value.get(), out);
        return;
    }
    if (auto* aa = dynamic_cast<AnnAssignNode*>(node)) {
        collectNestedScopeFreeVarsImpl(aa->target.get(), out);
        collectNestedScopeFreeVarsImpl(aa->annotation.get(), out);
        if (aa->value) collectNestedScopeFreeVarsImpl(aa->value.get(), out);
        return;
    }
    if (auto* aug = dynamic_cast<AugAssignNode*>(node)) {
        collectNestedScopeFreeVarsImpl(aug->target.get(), out);
        collectNestedScopeFreeVarsImpl(aug->value.get(), out);
        return;
    }
    if (auto* call = dynamic_cast<CallNode*>(node)) {
        collectNestedScopeFreeVarsImpl(call->func.get(), out);
        for (auto& arg : call->args) collectNestedScopeFreeVarsImpl(arg.get(), out);
        for (auto& kw : call->keywords) if (kw.second) collectNestedScopeFreeVarsImpl(kw.second.get(), out);
        return;
    }
    if (auto* ret = dynamic_cast<ReturnNode*>(node)) {
        if (ret->value) collectNestedScopeFreeVarsImpl(ret->value.get(), out);
        return;
    }
    if (auto* binop = dynamic_cast<BinOpNode*>(node)) {
        collectNestedScopeFreeVarsImpl(binop->left.get(), out);
        collectNestedScopeFreeVarsImpl(binop->right.get(), out);
        return;
    }
    if (auto* unop = dynamic_cast<UnaryOpNode*>(node)) {
        collectNestedScopeFreeVarsImpl(unop->operand.get(), out);
        return;
    }
    if (auto* ce = dynamic_cast<CondExprNode*>(node)) {
        collectNestedScopeFreeVarsImpl(ce->body.get(), out);
        collectNestedScopeFreeVarsImpl(ce->cond.get(), out);
        collectNestedScopeFreeVarsImpl(ce->orelse.get(), out);
        return;
    }
    if (auto* ce2 = dynamic_cast<ConditionalExprNode*>(node)) {
        collectNestedScopeFreeVarsImpl(ce2->body.get(), out);
        collectNestedScopeFreeVarsImpl(ce2->test.get(), out);
        collectNestedScopeFreeVarsImpl(ce2->orelse.get(), out);
        return;
    }
    if (auto* lst = dynamic_cast<ListLiteralNode*>(node)) {
        for (auto& e : lst->elements) collectNestedScopeFreeVarsImpl(e.get(), out);
        return;
    }
    if (auto* tup = dynamic_cast<TupleLiteralNode*>(node)) {
        for (auto& e : tup->elements) collectNestedScopeFreeVarsImpl(e.get(), out);
        return;
    }
    if (auto* st = dynamic_cast<SetLiteralNode*>(node)) {
        for (auto& e : st->elements) collectNestedScopeFreeVarsImpl(e.get(), out);
        return;
    }
    if (auto* d = dynamic_cast<DictLiteralNode*>(node)) {
        for (auto& k : d->keys)   if (k) collectNestedScopeFreeVarsImpl(k.get(), out);
        for (auto& v : d->values) collectNestedScopeFreeVarsImpl(v.get(), out);
        return;
    }
    if (auto* sub = dynamic_cast<SubscriptNode*>(node)) {
        collectNestedScopeFreeVarsImpl(sub->value.get(), out);
        collectNestedScopeFreeVarsImpl(sub->index.get(), out);
        return;
    }
    if (auto* att = dynamic_cast<AttributeNode*>(node)) {
        collectNestedScopeFreeVarsImpl(att->value.get(), out);
        return;
    }
    if (auto* sn = dynamic_cast<StarredNode*>(node)) {
        collectNestedScopeFreeVarsImpl(sn->value.get(), out);
        return;
    }
    if (auto* ne = dynamic_cast<NamedExprNode*>(node)) {
        collectNestedScopeFreeVarsImpl(ne->target.get(), out);
        collectNestedScopeFreeVarsImpl(ne->value.get(), out);
        return;
    }
    // NameNode, ConstantNode, etc.: nothing to recurse.
}

/** Returns non-empty string if body has dynamic locals access; reason for slot fallback. */
static std::string getDynamicLocalsReason(ASTNode* node) {
    if (!node) return "";
    // A function that references eval / exec / locals / vars anywhere
    // in its body must keep its locals on the frame (forceMapped) so
    // those builtins can see them.  The previous hand-rolled recursion
    // only descended into Suite / If / For / FunctionDef bodies, so
    // `return eval(...)`, `x = eval(...)`, a bare `eval(...)` statement
    // and uses inside while / try / with were all missed and the
    // function's frame was wrongly skipped.  collectUsedNames performs
    // the full recursive walk; over-detecting (e.g. `eval` used as a
    // plain name) only costs a frame allocation, never correctness.
    std::unordered_set<std::string> used;
    collectUsedNames(node, used);
    if (used.count("locals")) return "locals";
    if (used.count("exec")) return "exec";
    if (used.count("eval")) return "eval";
    if (used.count("vars")) return "vars";
    return "";
}

bool Compiler::hasDynamicLocalsAccess(ASTNode* node) {
    return !getDynamicLocalsReason(node).empty();
}

void Compiler::collectCapturedNames(ASTNode* node,
    const std::unordered_set<std::string>& globalsInScope,
    std::unordered_set<std::string>& capturedOut) {
    collectCapturedNamesImpl(node, globalsInScope, capturedOut, 0);
}

static void collectCapturedNamesImpl(ASTNode* node, const std::unordered_set<std::string>& globalsInScope, std::unordered_set<std::string>& capturedOut, int depth) {
    if (!node) return;

    if (auto* fn = dynamic_cast<FunctionDefNode*>(node)) {
        std::unordered_set<std::string> defined;
        std::unordered_set<std::string> used;
        std::unordered_set<std::string> nonlocals;
        
        // 1. Collect all names defined or used in the nested function
        collectDefinedNames(fn->body.get(), defined);
        for (const auto& p : fn->parameters) defined.insert(p);
        for (const auto& k : fn->kwonlyargs) defined.insert(k);
        if (!fn->vararg.empty()) defined.insert(fn->vararg);
        if (!fn->kwarg.empty()) defined.insert(fn->kwarg);
        
        collectUsedNames(fn->body.get(), used);
        collectNonlocalsFromNode(fn->body.get(), nonlocals);

        // 2. Names used but NOT defined here and NOT global are captured from outer scope
        for (const auto& name : used) {
            if (!defined.count(name) && !globalsInScope.count(name) && !nonlocals.count(name)) {
                capturedOut.insert(name);
            }
        }
        // No recursion into body because collectUsedNames ALREADY aggregates all used names deeply.
        // Recursing here bypasses our manual 'defined' intermediate maps and poisons closures.
    }

    if (auto* lam = dynamic_cast<LambdaNode*>(node)) {
        std::unordered_set<std::string> defined;
        for (const auto& p : lam->parameters) defined.insert(p);
        for (const auto& k : lam->kwonlyargs) defined.insert(k);
        if (!lam->vararg.empty()) defined.insert(lam->vararg);
        if (!lam->kwarg.empty()) defined.insert(lam->kwarg);

        std::unordered_set<std::string> used;
        collectUsedNames(lam->body.get(), used);
        
        std::unordered_set<std::string> nonlocals;
        collectNonlocalsFromNode(lam->body.get(), nonlocals);
        
        for (const auto& name : used) {
            if (!defined.count(name) && !globalsInScope.count(name) && !nonlocals.count(name)) {
                capturedOut.insert(name);
            }
        }
        // No recursion into body because collectUsedNames aggregates deep usage natively.
    }

    // Note: We deliberately DO NOT capture NameNode indiscriminately here based on depth > 0.
    // The explicit FunctionDefNode logic accurately traces 'used' vs 'defined'.

    // Recursive traversal for control flow nodes
    if (auto* s = dynamic_cast<SuiteNode*>(node)) {
        for (auto& st : s->statements) collectCapturedNamesImpl(st.get(), globalsInScope, capturedOut, depth);
    } else if (auto* iff = dynamic_cast<IfNode*>(node)) {
        collectCapturedNamesImpl(iff->body.get(), globalsInScope, capturedOut, depth);
        if (iff->orelse) collectCapturedNamesImpl(iff->orelse.get(), globalsInScope, capturedOut, depth);
    } else if (auto* f = dynamic_cast<ForNode*>(node)) {
        collectCapturedNamesImpl(f->body.get(), globalsInScope, capturedOut, depth);
    } else if (auto* af = dynamic_cast<AsyncForNode*>(node)) {
        collectCapturedNamesImpl(af->iter.get(), globalsInScope, capturedOut, depth);
        collectCapturedNamesImpl(af->target.get(), globalsInScope, capturedOut, depth);
        collectCapturedNamesImpl(af->body.get(), globalsInScope, capturedOut, depth);
        if (af->orelse) collectCapturedNamesImpl(af->orelse.get(), globalsInScope, capturedOut, depth);
    } else if (auto* w = dynamic_cast<WhileNode*>(node)) {
        collectCapturedNamesImpl(w->body.get(), globalsInScope, capturedOut, depth);
    } else if (auto* t = dynamic_cast<TryNode*>(node)) {
        collectCapturedNamesImpl(t->body.get(), globalsInScope, capturedOut, depth);
        for (auto& h : t->handlers) collectCapturedNamesImpl(h.body.get(), globalsInScope, capturedOut, depth);
        if (t->orelse) collectCapturedNamesImpl(t->orelse.get(), globalsInScope, capturedOut, depth);
        if (t->finalbody) collectCapturedNamesImpl(t->finalbody.get(), globalsInScope, capturedOut, depth);
    } else if (auto* a = dynamic_cast<AssignNode*>(node)) {
        collectCapturedNamesImpl(a->value.get(), globalsInScope, capturedOut, depth);
    } else if (auto* b = dynamic_cast<BinOpNode*>(node)) {
        collectCapturedNamesImpl(b->left.get(), globalsInScope, capturedOut, depth);
        collectCapturedNamesImpl(b->right.get(), globalsInScope, capturedOut, depth);
    } else if (auto* c = dynamic_cast<CallNode*>(node)) {
        if (auto* nm = dynamic_cast<NameNode*>(c->func.get())) {
            if (nm->id == "super" && c->args.empty() && c->keywords.empty()) {
                capturedOut.insert("__class__");
            }
        }
        collectCapturedNamesImpl(c->func.get(), globalsInScope, capturedOut, depth);
        for (auto& arg : c->args) collectCapturedNamesImpl(arg.get(), globalsInScope, capturedOut, depth);
        for (auto& kw : c->keywords) {
             if (kw.second) collectCapturedNamesImpl(kw.second.get(), globalsInScope, capturedOut, depth);
        }
    } else if (auto* ret = dynamic_cast<ReturnNode*>(node)) {
        collectCapturedNamesImpl(ret->value.get(), globalsInScope, capturedOut, depth);
    } else if (auto* cl = dynamic_cast<ClassDefNode*>(node)) {
        // Classes have their own scope in Python
        collectCapturedNamesImpl(cl->body.get(), globalsInScope, capturedOut, depth + 1);
    } else if (auto* att = dynamic_cast<AttributeNode*>(node)) {
        collectCapturedNamesImpl(att->value.get(), globalsInScope, capturedOut, depth);
    } else if (auto* sub = dynamic_cast<SubscriptNode*>(node)) {
        collectCapturedNamesImpl(sub->value.get(), globalsInScope, capturedOut, depth);
        collectCapturedNamesImpl(sub->index.get(), globalsInScope, capturedOut, depth);
    } else if (auto* sl = dynamic_cast<SliceNode*>(node)) {
        if (sl->start) collectCapturedNamesImpl(sl->start.get(), globalsInScope, capturedOut, depth);
        if (sl->stop) collectCapturedNamesImpl(sl->stop.get(), globalsInScope, capturedOut, depth);
        if (sl->step) collectCapturedNamesImpl(sl->step.get(), globalsInScope, capturedOut, depth);
    } else if (auto* u = dynamic_cast<UnaryOpNode*>(node)) {
        collectCapturedNamesImpl(u->operand.get(), globalsInScope, capturedOut, depth);
    } else if (auto* starred = dynamic_cast<StarredNode*>(node)) {
        collectCapturedNamesImpl(starred->value.get(), globalsInScope, capturedOut, depth);
    } else if (auto* named = dynamic_cast<NamedExprNode*>(node)) {
        collectCapturedNamesImpl(named->value.get(), globalsInScope, capturedOut, depth);
        collectCapturedNamesImpl(named->target.get(), globalsInScope, capturedOut, depth);
    } else if (auto* lst = dynamic_cast<ListLiteralNode*>(node)) {
        for (auto& e : lst->elements) collectCapturedNamesImpl(e.get(), globalsInScope, capturedOut, depth);
    } else if (auto* tup = dynamic_cast<TupleLiteralNode*>(node)) {
        for (auto& e : tup->elements) collectCapturedNamesImpl(e.get(), globalsInScope, capturedOut, depth);
    } else if (auto* d = dynamic_cast<DictLiteralNode*>(node)) {
        for (size_t i = 0; i < d->keys.size(); ++i) {
            if (d->keys[i]) collectCapturedNamesImpl(d->keys[i].get(), globalsInScope, capturedOut, depth);
            collectCapturedNamesImpl(d->values[i].get(), globalsInScope, capturedOut, depth);
        }
    } else if (auto* comp = dynamic_cast<ListCompNode*>(node)) {
        std::unordered_set<std::string> defined;
        for (const auto& gen : comp->generators) collectDefinedNames(gen.target.get(), defined);
        std::unordered_set<std::string> used;
        for (const auto& gen : comp->generators) {
            collectUsedNames(gen.iter.get(), used);
            collectUsedNames(gen.target.get(), used);
            for (const auto& i : gen.ifs) collectUsedNames(i.get(), used);
        }
        collectUsedNames(comp->elt.get(), used);
        for (const auto& name : used) {
            if (!defined.count(name) && !globalsInScope.count(name)) capturedOut.insert(name);
        }
    } else if (auto* comp = dynamic_cast<SetCompNode*>(node)) {
        std::unordered_set<std::string> defined;
        for (const auto& gen : comp->generators) collectDefinedNames(gen.target.get(), defined);
        std::unordered_set<std::string> used;
        for (const auto& gen : comp->generators) {
            collectUsedNames(gen.iter.get(), used);
            collectUsedNames(gen.target.get(), used);
            for (const auto& i : gen.ifs) collectUsedNames(i.get(), used);
        }
        collectUsedNames(comp->elt.get(), used);
        for (const auto& name : used) {
            if (!defined.count(name) && !globalsInScope.count(name)) capturedOut.insert(name);
        }
    } else if (auto* comp = dynamic_cast<GeneratorExpNode*>(node)) {
        std::unordered_set<std::string> defined;
        for (const auto& gen : comp->generators) collectDefinedNames(gen.target.get(), defined);
        std::unordered_set<std::string> used;
        for (const auto& gen : comp->generators) {
            collectUsedNames(gen.iter.get(), used);
            collectUsedNames(gen.target.get(), used);
            for (const auto& i : gen.ifs) collectUsedNames(i.get(), used);
        }
        collectUsedNames(comp->elt.get(), used);
        for (const auto& name : used) {
            if (!defined.count(name) && !globalsInScope.count(name)) capturedOut.insert(name);
        }
    } else if (auto* comp = dynamic_cast<DictCompNode*>(node)) {
        std::unordered_set<std::string> defined;
        for (const auto& gen : comp->generators) collectDefinedNames(gen.target.get(), defined);
        std::unordered_set<std::string> used;
        for (const auto& gen : comp->generators) {
            collectUsedNames(gen.iter.get(), used);
            collectUsedNames(gen.target.get(), used);
            for (const auto& i : gen.ifs) collectUsedNames(i.get(), used);
        }
        collectUsedNames(comp->key.get(), used);
        collectUsedNames(comp->value.get(), used);
        for (const auto& name : used) {
            if (!defined.count(name) && !globalsInScope.count(name)) capturedOut.insert(name);
        }
    } else if (auto* dc = dynamic_cast<DictCompNode*>(node)) {
        std::unordered_set<std::string> defined;
        for (const auto& gen : dc->generators) collectDefinedNames(gen.target.get(), defined);
        std::unordered_set<std::string> used;
        for (const auto& gen : dc->generators) {
            collectUsedNames(gen.iter.get(), used);
            collectUsedNames(gen.target.get(), used);
            for (const auto& i : gen.ifs) collectUsedNames(i.get(), used);
        }
        collectUsedNames(dc->key.get(), used);
        collectUsedNames(dc->value.get(), used);
        for (const auto& name : used) {
            if (!defined.count(name) && !globalsInScope.count(name)) capturedOut.insert(name);
        }
    } else if (auto* sc = dynamic_cast<SetCompNode*>(node)) {
        std::unordered_set<std::string> defined;
        for (const auto& gen : sc->generators) collectDefinedNames(gen.target.get(), defined);
        std::unordered_set<std::string> used;
        for (const auto& gen : sc->generators) {
            collectUsedNames(gen.iter.get(), used);
            collectUsedNames(gen.target.get(), used);
            for (const auto& i : gen.ifs) collectUsedNames(i.get(), used);
        }
        collectUsedNames(sc->elt.get(), used);
        for (const auto& name : used) {
            if (!defined.count(name) && !globalsInScope.count(name)) capturedOut.insert(name);
        }
    } else if (auto* ge = dynamic_cast<GeneratorExpNode*>(node)) {
        std::unordered_set<std::string> defined;
        for (const auto& gen : ge->generators) collectDefinedNames(gen.target.get(), defined);
        std::unordered_set<std::string> used;
        for (const auto& gen : ge->generators) {
            collectUsedNames(gen.iter.get(), used);
            collectUsedNames(gen.target.get(), used);
            for (const auto& i : gen.ifs) collectUsedNames(i.get(), used);
        }
        collectUsedNames(ge->elt.get(), used);
        for (const auto& name : used) {
            if (!defined.count(name) && !globalsInScope.count(name)) capturedOut.insert(name);
        }
    } else if (auto* bin = dynamic_cast<BinOpNode*>(node)) {
        collectCapturedNamesImpl(bin->left.get(), globalsInScope, capturedOut, depth);
        collectCapturedNamesImpl(bin->right.get(), globalsInScope, capturedOut, depth);
    } else if (auto* cond = dynamic_cast<ConditionalExprNode*>(node)) {
        collectCapturedNamesImpl(cond->test.get(), globalsInScope, capturedOut, depth);
        collectCapturedNamesImpl(cond->body.get(), globalsInScope, capturedOut, depth);
        collectCapturedNamesImpl(cond->orelse.get(), globalsInScope, capturedOut, depth);
    } else if (auto* js = dynamic_cast<JoinedStrNode*>(node)) {
        for (auto& v : js->values) collectCapturedNamesImpl(v.get(), globalsInScope, capturedOut, depth);
    } else if (auto* fv = dynamic_cast<FormattedValueNode*>(node)) {
        collectCapturedNamesImpl(fv->value.get(), globalsInScope, capturedOut, depth);
    } else if (auto* iff = dynamic_cast<IfNode*>(node)) {
        collectCapturedNamesImpl(iff->test.get(), globalsInScope, capturedOut, depth);
        collectCapturedNamesImpl(iff->body.get(), globalsInScope, capturedOut, depth);
        if (iff->orelse) collectCapturedNamesImpl(iff->orelse.get(), globalsInScope, capturedOut, depth);
    } else if (auto* wh = dynamic_cast<WhileNode*>(node)) {
        collectCapturedNamesImpl(wh->test.get(), globalsInScope, capturedOut, depth);
        collectCapturedNamesImpl(wh->body.get(), globalsInScope, capturedOut, depth);
        if (wh->orelse) collectCapturedNamesImpl(wh->orelse.get(), globalsInScope, capturedOut, depth);
    } else if (auto* fr = dynamic_cast<ForNode*>(node)) {
        collectCapturedNamesImpl(fr->iter.get(), globalsInScope, capturedOut, depth);
        collectCapturedNamesImpl(fr->target.get(), globalsInScope, capturedOut, depth);
        collectCapturedNamesImpl(fr->body.get(), globalsInScope, capturedOut, depth);
    } else if (auto* afr = dynamic_cast<AsyncForNode*>(node)) {
        collectCapturedNamesImpl(afr->iter.get(), globalsInScope, capturedOut, depth);
        collectCapturedNamesImpl(afr->target.get(), globalsInScope, capturedOut, depth);
        collectCapturedNamesImpl(afr->body.get(), globalsInScope, capturedOut, depth);
        if (afr->orelse) collectCapturedNamesImpl(afr->orelse.get(), globalsInScope, capturedOut, depth);
    } else if (auto* tr = dynamic_cast<TryNode*>(node)) {
        collectCapturedNamesImpl(tr->body.get(), globalsInScope, capturedOut, depth);
        for (auto& h : tr->handlers) {
            collectCapturedNamesImpl(h.type.get(), globalsInScope, capturedOut, depth);
            collectCapturedNamesImpl(h.body.get(), globalsInScope, capturedOut, depth);
        }
        if (tr->orelse) collectCapturedNamesImpl(tr->orelse.get(), globalsInScope, capturedOut, depth);
        if (tr->finalbody) collectCapturedNamesImpl(tr->finalbody.get(), globalsInScope, capturedOut, depth);
    } else if (auto* wn = dynamic_cast<WithNode*>(node)) {
        for (auto& item : wn->items) {
            collectCapturedNamesImpl(item.context_expr.get(), globalsInScope, capturedOut, depth);
            if (item.optional_vars) collectCapturedNamesImpl(item.optional_vars.get(), globalsInScope, capturedOut, depth);
        }
        collectCapturedNamesImpl(wn->body.get(), globalsInScope, capturedOut, depth);
    } else if (auto* awn = dynamic_cast<AsyncWithNode*>(node)) {
        for (auto& item : awn->items) {
            collectCapturedNamesImpl(item.context_expr.get(), globalsInScope, capturedOut, depth);
            if (item.optional_vars) collectCapturedNamesImpl(item.optional_vars.get(), globalsInScope, capturedOut, depth);
        }
        collectCapturedNamesImpl(awn->body.get(), globalsInScope, capturedOut, depth);
    } else if (auto* as = dynamic_cast<AssertNode*>(node)) {
        collectCapturedNamesImpl(as->test.get(), globalsInScope, capturedOut, depth);
        if (as->msg) collectCapturedNamesImpl(as->msg.get(), globalsInScope, capturedOut, depth);
    } else if (auto* r = dynamic_cast<RaiseNode*>(node)) {
        if (r->exc) collectCapturedNamesImpl(r->exc.get(), globalsInScope, capturedOut, depth);
        if (r->cause) collectCapturedNamesImpl(r->cause.get(), globalsInScope, capturedOut, depth);
    } else if (auto* y = dynamic_cast<YieldNode*>(node)) {
        if (y->value) collectCapturedNamesImpl(y->value.get(), globalsInScope, capturedOut, depth);
    } else if (auto* aw = dynamic_cast<AwaitNode*>(node)) {
        collectCapturedNamesImpl(aw->value.get(), globalsInScope, capturedOut, depth);
    } else if (auto* ass = dynamic_cast<AssignNode*>(node)) {
        for (auto& t : ass->targets) collectCapturedNamesImpl(t.get(), globalsInScope, capturedOut, depth);
        collectCapturedNamesImpl(ass->value.get(), globalsInScope, capturedOut, depth);
    } else if (auto* aan = dynamic_cast<AnnAssignNode*>(node)) {
        collectCapturedNamesImpl(aan->target.get(), globalsInScope, capturedOut, depth);
        collectCapturedNamesImpl(aan->annotation.get(), globalsInScope, capturedOut, depth);
        if (aan->value) collectCapturedNamesImpl(aan->value.get(), globalsInScope, capturedOut, depth);
    } else if (auto* aug = dynamic_cast<AugAssignNode*>(node)) {
        collectCapturedNamesImpl(aug->target.get(), globalsInScope, capturedOut, depth);
        collectCapturedNamesImpl(aug->value.get(), globalsInScope, capturedOut, depth);
    } else if (auto* s = dynamic_cast<SuiteNode*>(node)) {
        for (auto& st : s->statements) collectCapturedNamesImpl(st.get(), globalsInScope, capturedOut, depth);
    } else if (auto* nm = dynamic_cast<NameNode*>(node)) {
        // Bare name reference: candidate for capture from an enclosing scope.
        // The caller filters by isLocal/isInOuterScope, so collecting all names is safe.
        if (!globalsInScope.count(nm->id)) capturedOut.insert(nm->id);
    }
}

bool Compiler::compileSuite(SuiteNode* n) {
    if (!n) return false;
    if (n->statements.empty()) return true;
    for (size_t i = 0; i < n->statements.size(); ++i) {
        // CPython: the first bare string literal of a function body is the
        // docstring. Capture it BEFORE compileNode emits the LOAD_CONST so
        // we can also stamp it onto the resulting code object as `co_doc`
        // (read by ExecutionEngine when binding `fn.__doc__`).
        if (isFunctionScope_ && !isClassBody_ && i == 0 && capturedDocstring_.empty()) {
            auto* c = dynamic_cast<ConstantNode*>(n->statements[i].get());
            if (c && c->constType == ConstantNode::ConstType::Str) {
                capturedDocstring_ = c->strVal;
            }
        }
        if (!compileNode(n->statements[i].get())) return false;
        if (statementLeavesValue(n->statements[i].get())) {
            // In a class body, the first string literal expression becomes the docstring.
            if (isClassBody_ && i == 0) {
                auto* c = dynamic_cast<ConstantNode*>(n->statements[i].get());
                if (c && c->constType == ConstantNode::ConstType::Str) {
                    emitNameOp("__doc__", TargetCtx::Store);
                    continue;
                }
            }
            emit(OP_POP_TOP, 0);
        }
    }
    return true;
}

// Walk a function body collecting names that appear as AnnAssignNode
// targets WITHOUT an initial value (`x: int` with no `= ...`).  These
// locals are declared bindable but not actually bound, so accessing
// them before any assignment must raise UnboundLocalError per
// PEP 526.  compileFunctionDef stores the env's unbound sentinel into
// each such slot at function entry; LOAD_FAST detects the sentinel
// and raises.
static void collectAnnotationOnlyLocals(ASTNode* node, std::unordered_set<std::string>& out) {
    if (!node) return;
    if (auto* a = dynamic_cast<AnnAssignNode*>(node)) {
        if (!a->value) {
            if (auto* nm = dynamic_cast<NameNode*>(a->target.get())) {
                // `(name): T` does not declare a local in CPython, only
                // `name: T` does.  Skip the parenthesized form.
                if (!nm->parenthesized) out.insert(nm->id);
            }
        }
        return;
    }
    if (auto* s = dynamic_cast<SuiteNode*>(node)) {
        for (auto& st : s->statements) collectAnnotationOnlyLocals(st.get(), out);
        return;
    }
    if (auto* iff = dynamic_cast<IfNode*>(node)) {
        collectAnnotationOnlyLocals(iff->body.get(), out);
        if (iff->orelse) collectAnnotationOnlyLocals(iff->orelse.get(), out);
        return;
    }
    if (auto* w = dynamic_cast<WhileNode*>(node)) {
        collectAnnotationOnlyLocals(w->body.get(), out);
        if (w->orelse) collectAnnotationOnlyLocals(w->orelse.get(), out);
        return;
    }
    if (auto* f = dynamic_cast<ForNode*>(node)) {
        collectAnnotationOnlyLocals(f->body.get(), out);
        if (f->orelse) collectAnnotationOnlyLocals(f->orelse.get(), out);
        return;
    }
    if (auto* af = dynamic_cast<AsyncForNode*>(node)) {
        collectAnnotationOnlyLocals(af->body.get(), out);
        if (af->orelse) collectAnnotationOnlyLocals(af->orelse.get(), out);
        return;
    }
    if (auto* tr = dynamic_cast<TryNode*>(node)) {
        collectAnnotationOnlyLocals(tr->body.get(), out);
        for (auto& h : tr->handlers) collectAnnotationOnlyLocals(h.body.get(), out);
        if (tr->orelse) collectAnnotationOnlyLocals(tr->orelse.get(), out);
        if (tr->finalbody) collectAnnotationOnlyLocals(tr->finalbody.get(), out);
        return;
    }
    if (auto* wn = dynamic_cast<WithNode*>(node)) {
        collectAnnotationOnlyLocals(wn->body.get(), out);
        return;
    }
    if (auto* awn = dynamic_cast<AsyncWithNode*>(node)) {
        collectAnnotationOnlyLocals(awn->body.get(), out);
        return;
    }
    // Don't descend into nested functions/classes — they have their own scope.
}

// Names assigned in a body via plain Assign / for-target / etc.  Used to
// distinguish "annotated AND assigned" from "annotated only" — the
// former should NOT receive the unbound sentinel since a real STORE
// will run later in the body.
static void collectStoreAssignedNames(ASTNode* node, std::unordered_set<std::string>& out) {
    if (!node) return;
    if (auto* a = dynamic_cast<AssignNode*>(node)) {
        for (auto& t : a->targets) {
            if (auto* nm = dynamic_cast<NameNode*>(t.get())) out.insert(nm->id);
        }
        return;
    }
    if (auto* aug = dynamic_cast<AugAssignNode*>(node)) {
        if (auto* nm = dynamic_cast<NameNode*>(aug->target.get())) out.insert(nm->id);
        return;
    }
    if (auto* aa = dynamic_cast<AnnAssignNode*>(node)) {
        if (aa->value) {
            if (auto* nm = dynamic_cast<NameNode*>(aa->target.get())) out.insert(nm->id);
        }
        return;
    }
    if (auto* s = dynamic_cast<SuiteNode*>(node)) {
        for (auto& st : s->statements) collectStoreAssignedNames(st.get(), out);
        return;
    }
    if (auto* iff = dynamic_cast<IfNode*>(node)) {
        collectStoreAssignedNames(iff->body.get(), out);
        if (iff->orelse) collectStoreAssignedNames(iff->orelse.get(), out);
        return;
    }
    if (auto* w = dynamic_cast<WhileNode*>(node)) {
        collectStoreAssignedNames(w->body.get(), out);
        if (w->orelse) collectStoreAssignedNames(w->orelse.get(), out);
        return;
    }
    if (auto* f = dynamic_cast<ForNode*>(node)) {
        if (auto* nm = dynamic_cast<NameNode*>(f->target.get())) out.insert(nm->id);
        collectStoreAssignedNames(f->body.get(), out);
        if (f->orelse) collectStoreAssignedNames(f->orelse.get(), out);
        return;
    }
    if (auto* af = dynamic_cast<AsyncForNode*>(node)) {
        if (auto* nm = dynamic_cast<NameNode*>(af->target.get())) out.insert(nm->id);
        collectStoreAssignedNames(af->body.get(), out);
        if (af->orelse) collectStoreAssignedNames(af->orelse.get(), out);
        return;
    }
    if (auto* tr = dynamic_cast<TryNode*>(node)) {
        collectStoreAssignedNames(tr->body.get(), out);
        for (auto& h : tr->handlers) {
            if (!h.name.empty()) out.insert(h.name);
            collectStoreAssignedNames(h.body.get(), out);
        }
        if (tr->orelse) collectStoreAssignedNames(tr->orelse.get(), out);
        if (tr->finalbody) collectStoreAssignedNames(tr->finalbody.get(), out);
        return;
    }
    if (auto* wn = dynamic_cast<WithNode*>(node)) {
        for (auto& item : wn->items) {
            if (item.optional_vars) {
                if (auto* nm = dynamic_cast<NameNode*>(item.optional_vars.get())) out.insert(nm->id);
            }
        }
        collectStoreAssignedNames(wn->body.get(), out);
        return;
    }
    if (auto* awn = dynamic_cast<AsyncWithNode*>(node)) {
        for (auto& item : awn->items) {
            if (item.optional_vars) {
                if (auto* nm = dynamic_cast<NameNode*>(item.optional_vars.get())) out.insert(nm->id);
            }
        }
        collectStoreAssignedNames(awn->body.get(), out);
        return;
    }
    // Nested defs/classes bind their own name in this scope.
    if (auto* fd = dynamic_cast<FunctionDefNode*>(node)) { out.insert(fd->name); return; }
    if (auto* afd = dynamic_cast<AsyncFunctionDefNode*>(node)) { out.insert(afd->name); return; }
    if (auto* cd = dynamic_cast<ClassDefNode*>(node)) { out.insert(cd->name); return; }
    if (auto* imp = dynamic_cast<ImportNode*>(node)) {
        if (!imp->alias.empty()) out.insert(imp->alias);
        return;
    }
    if (auto* impf = dynamic_cast<ImportFromNode*>(node)) {
        for (auto& nm : impf->names) {
            const std::string& bound = nm.second.empty() ? nm.first : nm.second;
            out.insert(bound);
        }
        return;
    }
}

// Walk a function body collecting names that appear as AnnAssignNode
// targets (i.e. `x: int [= ...]`).  Used to enforce CPython's rule that
// a name cannot be both annotated and declared global/nonlocal in the
// same function — `def f(): x: int; global x` is SyntaxError.
static void collectAnnotatedNames(ASTNode* node, std::unordered_set<std::string>& out) {
    if (!node) return;
    if (auto* a = dynamic_cast<AnnAssignNode*>(node)) {
        if (auto* nm = dynamic_cast<NameNode*>(a->target.get())) {
            out.insert(nm->id);
        }
        return;
    }
    if (auto* s = dynamic_cast<SuiteNode*>(node)) {
        for (auto& st : s->statements) collectAnnotatedNames(st.get(), out);
        return;
    }
    if (auto* iff = dynamic_cast<IfNode*>(node)) {
        collectAnnotatedNames(iff->body.get(), out);
        if (iff->orelse) collectAnnotatedNames(iff->orelse.get(), out);
        return;
    }
    if (auto* w = dynamic_cast<WhileNode*>(node)) {
        collectAnnotatedNames(w->body.get(), out);
        if (w->orelse) collectAnnotatedNames(w->orelse.get(), out);
        return;
    }
    if (auto* f = dynamic_cast<ForNode*>(node)) {
        collectAnnotatedNames(f->body.get(), out);
        if (f->orelse) collectAnnotatedNames(f->orelse.get(), out);
        return;
    }
    if (auto* af = dynamic_cast<AsyncForNode*>(node)) {
        collectAnnotatedNames(af->body.get(), out);
        if (af->orelse) collectAnnotatedNames(af->orelse.get(), out);
        return;
    }
    if (auto* tr = dynamic_cast<TryNode*>(node)) {
        collectAnnotatedNames(tr->body.get(), out);
        for (auto& h : tr->handlers) collectAnnotatedNames(h.body.get(), out);
        if (tr->orelse) collectAnnotatedNames(tr->orelse.get(), out);
        if (tr->finalbody) collectAnnotatedNames(tr->finalbody.get(), out);
        return;
    }
    if (auto* wn = dynamic_cast<WithNode*>(node)) {
        collectAnnotatedNames(wn->body.get(), out);
        return;
    }
    if (auto* awn = dynamic_cast<AsyncWithNode*>(node)) {
        collectAnnotatedNames(awn->body.get(), out);
        return;
    }
    // Don't descend into nested function/class bodies — they have their
    // own scope and their annotations don't conflict with outer
    // global/nonlocal decls of the same name.
}

bool Compiler::compileFunctionDef(FunctionDefNode* n) {
    if (!n) return false;
    std::unordered_set<std::string> bodyGlobals;
    std::unordered_set<std::string> bodyNonlocals;
    std::vector<std::string> localsOrdered;
    collectLocalsFromBody(n->body.get(), bodyGlobals, bodyNonlocals, localsOrdered);

    // CPython: a name annotated in the function body cannot also be
    // declared `global` or `nonlocal` in the same function — that combo
    // is a SyntaxError.  Detect it before bytecode emission.  Function
    // parameters that have parameter_annotations are NOT included here
    // (parameters are always locals to the function — the symtable rule
    // only applies to body-level `name: T` statements).
    {
        std::unordered_set<std::string> annotated;
        collectAnnotatedNames(n->body.get(), annotated);
        for (const auto& name : annotated) {
            if (bodyGlobals.count(name) || bodyNonlocals.count(name)) {
                return false;  // surfaced as SyntaxError by py_compile
            }
        }
    }
    std::vector<std::string> params;
    if (!n->parameters.empty()) {
        for (const auto& p : n->parameters) params.push_back(p);
    }
    std::unordered_set<std::string> combinedGlobals = bodyGlobals;
    for (const auto& g : globalNames_) combinedGlobals.insert(g);

    std::unordered_set<std::string> captured;
    collectCapturedNames(n->body.get(), combinedGlobals, captured);
    // `collectCapturedNames` walks nested function/lambda/comprehension
    // scopes and returns *their* free variables.  It does NOT include the
    // free variables of the function we are compiling *itself* — e.g.
    // `c` referenced directly inside `inner`'s top-level `for x in c.get(...):`
    // with no further nesting.  Add those here: any name used in the body
    // that is neither defined locally nor declared global must be treated
    // as a candidate free variable.
    {
        std::unordered_set<std::string> bodyUsed;
        collectUsedNames(n->body.get(), bodyUsed);
        std::unordered_set<std::string> bodyDefined;
        collectDefinedNames(n->body.get(), bodyDefined);
        for (const auto& p : params) bodyDefined.insert(p);
        for (const auto& k : n->kwonlyargs) bodyDefined.insert(k);
        if (!n->vararg.empty()) bodyDefined.insert(n->vararg);
        if (!n->kwarg.empty()) bodyDefined.insert(n->kwarg);
        for (const auto& name : bodyUsed) {
            if (bodyDefined.count(name) || combinedGlobals.count(name) || bodyNonlocals.count(name))
                continue;
            captured.insert(name);
        }
    }
    // Captured names that are NOT defined in this function are its nonlocals,
    // but ONLY if they are actually available as locals or nonlocals in the
    // enclosing scope. Names that appear only in module-level globals or builtins
    // must NOT become LOAD_DEREF in this function — they remain LOAD_GLOBAL/LOAD_NAME.
    for (const auto& c : captured) {
        bool isLocal = false;
        for (const auto& p : params) if (p == c) isLocal = true;
        for (const auto& kw : n->kwonlyargs) if (kw == c) isLocal = true;
        if (n->vararg == c) isLocal = true;
        if (n->kwarg == c) isLocal = true;
        for (const auto& l : localsOrdered) if (l == c) isLocal = true;
        if (!isLocal) {
            // Only treat as nonlocal if the outer scope actually has it as a
            // local or nonlocal (i.e. it can be closed over via a cell).
            // If the outer scope has no such binding, the name is a global/builtin
            // and must be resolved via LOAD_GLOBAL/LOAD_NAME, not LOAD_DEREF.
            bool isInOuterScope = localSlotMap_.count(c) || definedLocals_.count(c) || nonlocalNames_.count(c);
            if (isInOuterScope) bodyNonlocals.insert(c);
        }
    }

    // PG: zero-arg super() inside a method emits an implicit load of
    // the enclosing class name, but that load is generated at code
    // emission time and not visible to collectCapturedNames.  If this
    // function is a class method (isClassBody_ outer) and the
    // currentClassName_ is bound in an enclosing scope, explicitly add
    // it to bodyNonlocals so emitNameOp picks LOAD_DEREF over
    // LOAD_GLOBAL.  Module-level classes still resolve fine via
    // LOAD_GLOBAL.
    if (isClassBody_ && !currentClassName_.empty()) {
        const std::string& cn = currentClassName_;
        bool isLocal = false;
        for (const auto& p : params) if (p == cn) isLocal = true;
        for (const auto& kw : n->kwonlyargs) if (kw == cn) isLocal = true;
        if (n->vararg == cn) isLocal = true;
        if (n->kwarg == cn) isLocal = true;
        for (const auto& l : localsOrdered) if (l == cn) isLocal = true;
        if (!isLocal) {
            bool isInOuterScope = localSlotMap_.count(cn) || definedLocals_.count(cn) || nonlocalNames_.count(cn);
            if (isInOuterScope) bodyNonlocals.insert(cn);
        }
    }

    std::string dynamicReason = getDynamicLocalsReason(n->body.get());
    bool forceMapped = !dynamicReason.empty();

    // Cell semantics for closures: if any nested scope (def, lambda,
    // comprehension, generator-exp) reads a name that this function
    // defines as a local/parameter, route this function's locals to
    // the frame's attribute table.  The inner closure's frame chains
    // to this frame as a parent, so its LOAD_DEREF walks up and
    // observes mutations live — which is the cell semantics CPython
    // gives via explicit cell objects.
    if (!forceMapped) {
        std::unordered_set<std::string> innerFrees;
        collectNestedScopeFreeVarsImpl(n->body.get(), innerFrees);
        bool capturesOwnLocal = false;
        for (const auto& name : innerFrees) {
            if (combinedGlobals.count(name)) continue;
            for (const auto& l : localsOrdered) if (l == name) { capturesOwnLocal = true; break; }
            if (capturesOwnLocal) break;
            for (const auto& p : params) if (p == name) { capturesOwnLocal = true; break; }
            if (capturesOwnLocal) break;
            for (const auto& kw : n->kwonlyargs) if (kw == name) { capturesOwnLocal = true; break; }
            if (capturesOwnLocal) break;
            if (!n->vararg.empty() && name == n->vararg) { capturesOwnLocal = true; break; }
            if (!n->kwarg.empty()  && name == n->kwarg)  { capturesOwnLocal = true; break; }
        }
        if (capturesOwnLocal) {
            dynamicReason = "captured-by-inner-closure";
            forceMapped = true;
        }
    }

    if (get_env_diag()) {
        fprintf(stderr, "DEBUG COMPILER: FunctionDef '%s' forceMapped=%d dynamicReason='%s' captured_size=%zu\n",
                n->name.c_str(), (int)forceMapped, dynamicReason.c_str(), captured.size());
        for (const auto& c : captured) {
            fprintf(stderr, "  - captured: %s\n", c.c_str());
        }
        fflush(stderr);
    }

    std::vector<std::string> varnamesOrdered;
    // 1. Positional Parameters
    for (const auto& p : params) {
        varnamesOrdered.push_back(p);
    }
    // 2. Keyword-only Parameters
    for (const auto& kw : n->kwonlyargs) {
        bool alreadyIn = false;
        for (const auto& v : varnamesOrdered) if (v == kw) alreadyIn = true;
        if (!alreadyIn) varnamesOrdered.push_back(kw);
    }
    // 3. vararg and kwarg
    if (!n->vararg.empty()) {
        bool alreadyIn = false;
        for (const auto& v : varnamesOrdered) if (v == n->vararg) alreadyIn = true;
        if (!alreadyIn) varnamesOrdered.push_back(n->vararg);
    }
    if (!n->kwarg.empty()) {
        bool alreadyIn = false;
        for (const auto& v : varnamesOrdered) if (v == n->kwarg) alreadyIn = true;
        if (!alreadyIn) varnamesOrdered.push_back(n->kwarg);
    }

    // 4. Then other locals
    for (const auto& loc : localsOrdered) {
        bool alreadyIn = false;
        for (const auto& v : varnamesOrdered) if (v == loc) alreadyIn = true;
        if (!alreadyIn) varnamesOrdered.push_back(loc);
    }

    std::unordered_map<std::string, int> slotMap;
    int automatic_count = 0;
    if (!forceMapped) {
        for (size_t i = 0; i < varnamesOrdered.size(); ++i) {
            slotMap[varnamesOrdered[i]] = static_cast<int>(i);
        }
        automatic_count = static_cast<int>(varnamesOrdered.size()); // finalized after body compilation
    }

    int nparams = static_cast<int>(params.size());
    if (get_env_diag()) {
        fprintf(stderr, "DEBUG COMPILER: %s:%d FunctionDef '%s' nparams=%d\n", filename_.c_str(), n->line, n->name.c_str(), nparams);
        fflush(stderr);
    }
    int kwonlyargcount = static_cast<int>(n->kwonlyargs.size());

    Compiler bodyCompiler(ctx_, filename_);
    bodyCompiler.globalNames_ = globalNames_;
    for (const auto& g : bodyGlobals) bodyCompiler.globalNames_.insert(g);
    bodyCompiler.nonlocalNames_ = bodyNonlocals;
    bodyCompiler.localSlotMap_ = slotMap;
    // Track defined locals independent of physical storage (slot vs frame).
    for (const auto& v : varnamesOrdered) bodyCompiler.definedLocals_.insert(v);
    bodyCompiler.isFunctionScope_ = true;
    bodyCompiler.forceMapped_ = forceMapped;
    // Only propagate currentClassName_ when this function is a direct class method
    // (i.e., defined inside a class body). Nested functions inside methods do not
    // inherit the class name to avoid false super() rewrites.
    if (isClassBody_) bodyCompiler.currentClassName_ = currentClassName_;

    // CPython qualname rules for nested defs:
    //   class C:        prefix=""        → method qualname "C.method"
    //     def method:   prefix="C"
    //       def inner:  prefix="C.method.<locals>" → "C.method.<locals>.inner"
    // Build *this* function's qualname from the current prefix, then push
    // "<thisQualname>.<locals>" as the prefix the body sees so any nested
    // defs inside compose correctly.
    std::string fnQualname = qualnamePrefix_.empty()
        ? n->name
        : qualnamePrefix_ + "." + n->name;
    bodyCompiler.qualnamePrefix_ = fnQualname + ".<locals>";

    // Pre-bind annotation-only locals (`x: int` with no value) to the
    // env's "<unbound>" sentinel.  CO_OPTIMIZED slots are otherwise
    // initialised to PROTO_NONE, which is indistinguishable from an
    // explicit `x = None` — but CPython requires that reading an
    // annotated-but-never-assigned local raises UnboundLocalError, not
    // returns None.  LOAD_FAST detects this sentinel and raises.
    if (!forceMapped) {
        std::unordered_set<std::string> annOnly;
        collectAnnotationOnlyLocals(n->body.get(), annOnly);
        if (!annOnly.empty()) {
            std::unordered_set<std::string> assigned;
            collectStoreAssignedNames(n->body.get(), assigned);
            // Parameters are always bound at function entry — never sentinel.
            for (const auto& p : params) assigned.insert(p);
            for (const auto& kw : n->kwonlyargs) assigned.insert(kw);
            if (!n->vararg.empty()) assigned.insert(n->vararg);
            if (!n->kwarg.empty()) assigned.insert(n->kwarg);
            PythonEnvironment* env = PythonEnvironment::fromContext(ctx_);
            const proto::ProtoObject* sentinel = env ? env->getUnboundSentinel() : nullptr;
            if (sentinel) {
                int sentIdx = bodyCompiler.addConstant(sentinel);
                for (const auto& name : annOnly) {
                    if (assigned.count(name)) continue;  // some path will store a real value
                    auto it = bodyCompiler.localSlotMap_.find(name);
                    if (it == bodyCompiler.localSlotMap_.end()) continue;
                    bodyCompiler.emit(OP_LOAD_CONST, sentIdx);
                    bodyCompiler.emit(OP_STORE_FAST, it->second);
                }
            }
        }
    }

    if (!bodyCompiler.compileNode(n->body.get())) return false;
    if (!forceMapped) automatic_count += bodyCompiler.getMaxStack() + 128;

    int noneIdx = bodyCompiler.addConstant(PROTO_NONE);
    bodyCompiler.emit(OP_LOAD_CONST, noneIdx);
    bodyCompiler.emit(OP_RETURN_VALUE);

    bodyCompiler.applyPatches();

    int co_flags = CO_NEWLOCALS;
    if (!forceMapped) co_flags |= CO_OPTIMIZED;
    if (!captured.empty()) co_flags |= CO_NESTED;
    if (!n->vararg.empty()) co_flags |= CO_VARARGS;
    if (!n->kwarg.empty()) co_flags |= CO_VARKEYWORDS;

    std::vector<const proto::ProtoObject*> varnamesVec;
    varnamesVec.reserve(varnamesOrdered.size());
    for (const auto& name : varnamesOrdered)
        varnamesVec.push_back(PythonEnvironment::getInternedString(ctx_, name.c_str())->asObject(ctx_));
    const proto::ProtoTuple* co_varnames_list = ctx_->newTuple(varnamesVec);

    const proto::ProtoTuple* co_lnotab = bodyCompiler.getLnotab();

    const proto::ProtoObject* codeObj = makeCodeObject(ctx_, bodyCompiler.getConstants(), bodyCompiler.getNames(), bodyCompiler.getBytecode(), PythonEnvironment::getInternedString(ctx_, filename_.c_str()), co_varnames_list, nparams, kwonlyargcount, automatic_count, co_flags, bodyCompiler.isGenerator_, PythonEnvironment::getInternedString(ctx_, n->name.c_str()), bodyCompiler.firstLine_, co_lnotab);
    if (!codeObj) return false;
    // Stamp `co_freevars` — the exact set of names this function (and its
    // nested scopes) close over from enclosing *function* scopes.
    // bodyNonlocals already excludes this function's own params/locals
    // (the `isLocal` filter above), so OP_BUILD_FUNCTION can snapshot
    // ONLY these names from the outer frame instead of blindly copying
    // every outer local — the blind copy leaks an enclosing `self` into
    // a nested method's closure frame and shadows the real binding.
    {
        std::vector<const proto::ProtoObject*> freeVec;
        freeVec.reserve(bodyNonlocals.size());
        for (const auto& fv : bodyNonlocals)
            freeVec.push_back(PythonEnvironment::getInternedString(ctx_, fv.c_str())->asObject(ctx_));
        codeObj = codeObj->setAttribute(ctx_,
            PythonEnvironment::getInternedString(ctx_, "co_freevars"),
            ctx_->newTuple(freeVec)->asObject(ctx_));
    }
    // Stamp `co_doc` on the code object when the body's first statement was
    // a string literal; ExecutionEngine reads it to populate `fn.__doc__`.
    if (!bodyCompiler.capturedDocstring_.empty()) {
        codeObj = codeObj->setAttribute(ctx_,
            PythonEnvironment::getInternedString(ctx_, "co_doc"),
            PythonEnvironment::getInternedString(ctx_, bodyCompiler.capturedDocstring_.c_str())->asObject(ctx_));
    }
    // Stamp `co_qualname` so the function builder can populate
    // `fn.__qualname__` to the dotted, nested-aware form CPython produces
    // (e.g. `C.method.<locals>.inner`). Module-level defs and class methods
    // pick up a meaningful prefix from `qualnamePrefix_`; nested function
    // defs get the `<parent>.<locals>.` chain via the bodyCompiler's
    // forwarded prefix above.
    codeObj = codeObj->setAttribute(ctx_,
        PythonEnvironment::getInternedString(ctx_, "co_qualname"),
        PythonEnvironment::getInternedString(ctx_, fnQualname.c_str())->asObject(ctx_));
    int idx = addConstant(codeObj);
    emit(OP_LOAD_CONST, idx);

    int make_fn_flags = 0;
    if (!n->defaults.empty()) {
        for (auto& d : n->defaults) {
            if (!compileNode(d.get())) return false;
        }
        emit(OP_BUILD_TUPLE, static_cast<int>(n->defaults.size()));
        make_fn_flags |= 0x01;
    }
    if (!n->kw_defaults.empty()) {
        int actual_count = 0;
        for (size_t i = 0; i < n->kw_defaults.size() && i < n->kwonlyargs.size(); ++i) {
            if (n->kw_defaults[i]) {
                const std::string& name = n->kwonlyargs[i];
                int nameIdx = addConstant(PythonEnvironment::getInternedString(ctx_, name.c_str())->asObject(ctx_));
                emit(OP_LOAD_CONST, nameIdx);
                if (!compileNode(n->kw_defaults[i].get())) return false;
                actual_count++;
            }
        }
        if (actual_count > 0) {
            emit(OP_BUILD_MAP, actual_count);
            make_fn_flags |= 0x02;
        }
    }

    // Function annotations: build {param_name: type, ..., 'return': ret_type}
    // wrapped in a SETUP_FINALLY so that any annotation expression that
    // raises (typically a forward reference like `def f() -> IO[X]:` where
    // IO is defined later in the same module) is silently skipped.  PEP 649
    // makes annotations lazy in CPython 3.14; protoPython evaluates eagerly
    // but absorbs failures so module imports proceed even when individual
    // annotations can't be resolved at function-def time.  Bit 0x04 of
    // make_fn_flags tells OP_BUILD_FUNCTION to pop the dict and attach it
    // as `__annotations__` on the resulting function object.
    int ann_total = static_cast<int>(n->parameter_annotations.size()) + (n->returns ? 1 : 0);
    if (ann_total > 0) {
        // try { build map } except { make empty dict }
        emit(OP_SETUP_FINALLY, 0);
        int handlerSlot = bytecodeOffset() - 1;
        // Helper: name-mangle `__name` style parameter names when this
        // function is defined inside a class body, matching CPython's
        // PEP 8 mangling rule.  Mirrors the same code path used by
        // compileAnnAssign for class-level annotations.
        auto mangle = [&](const std::string& raw) -> std::string {
            if (currentClassName_.empty()) return raw;
            if (raw.size() < 2 || raw[0] != '_' || raw[1] != '_') return raw;
            // Names ending in __ are dunders — never mangled.
            if (raw.size() >= 4 && raw[raw.size()-1] == '_' && raw[raw.size()-2] == '_') return raw;
            std::string cls = currentClassName_;
            size_t i = 0;
            while (i < cls.size() && cls[i] == '_') ++i;
            cls = cls.substr(i);
            if (cls.empty()) return raw;
            return "_" + cls + raw;
        };
        for (auto& kv : n->parameter_annotations) {
            std::string keyName = mangle(kv.first);
            int nameIdx = addConstant(PythonEnvironment::getInternedString(ctx_, keyName.c_str())->asObject(ctx_));
            emit(OP_LOAD_CONST, nameIdx);
            if (!compileNode(kv.second.get())) return false;
        }
        if (n->returns) {
            int nameIdx = addConstant(PythonEnvironment::getInternedString(ctx_, "return")->asObject(ctx_));
            emit(OP_LOAD_CONST, nameIdx);
            if (!compileNode(n->returns.get())) return false;
        }
        emit(OP_BUILD_MAP, ann_total);
        emit(OP_POP_BLOCK);
        emit(OP_JUMP_ABSOLUTE, 0);
        int doneSlot = bytecodeOffset() - 1;
        // Handler: exception arrived as 3 stack items (type, value, tb).
        // Drop them and produce an empty dict so the function still has
        // `__annotations__ == {}` rather than no attribute at all.
        addPatch(handlerSlot, bytecodeOffset());
        emit(OP_POP_TOP);
        emit(OP_POP_TOP);
        emit(OP_POP_TOP);
        emit(OP_BUILD_MAP, 0);
        addPatch(doneSlot, bytecodeOffset());
        make_fn_flags |= 0x04;
    }

    emit(OP_BUILD_FUNCTION, make_fn_flags);

    // Apply decorators Bottom-to-Top.  CPython 3.11+ CALL ABI expects
    // [NULL|self, callable, arg1, ..., argN] on the operand stack.
    // Emit PUSH_NULL/ROT_TWO so the slot beneath `decorator` is a
    // genuine NULL marker; otherwise CALL_FUNCTION's "modern" branch
    // mistakes whatever was below `fn` (e.g. an iterator left by an
    // enclosing for loop) for `self` and routes the call as a method
    // dispatch — yielding `iterator(decorator, fn)` and the
    // characteristic "<tuple_iterator object> object is not callable"
    // TypeError.
    if (!n->decorator_list.empty()) {
        for (auto it = n->decorator_list.rbegin(); it != n->decorator_list.rend(); ++it) {
            // Stack here: [..., fn]
            emit(OP_PUSH_NULL, 0);                   // [..., fn, NULL]
            emit(OP_ROT_TWO, 0);                     // [..., NULL, fn]
            if (!compileNode(it->get())) return false; // [..., NULL, fn, decorator]
            emit(OP_ROT_TWO, 0);                     // [..., NULL, decorator, fn]
            emit(OP_CALL_FUNCTION, 1);               // [..., result]
        }
    }

    return emitNameOp(n->name, TargetCtx::Store);
}

bool Compiler::compileLambda(LambdaNode* n) {
    if (!n) return false;
    
    std::unordered_set<std::string> bodyGlobals;
    std::vector<std::string> localsOrdered;
    std::vector<std::string> params = n->parameters;
    
    std::unordered_set<std::string> combinedGlobals = bodyGlobals;
    for (const auto& g : globalNames_) combinedGlobals.insert(g);

    std::unordered_set<std::string> captured;
    collectCapturedNames(n->body.get(), combinedGlobals, captured);
    
    // Only treat a captured name as nonlocal (LOAD_DEREF) when the
    // enclosing scope actually has it as a local or nonlocal — i.e. it
    // can be closed over via a cell.  Builtins and module-level globals
    // must resolve via LOAD_GLOBAL, not LOAD_DEREF.  This matters for
    // top-level lambdas inside `eval(code, namespace)` (and exec): the
    // outer scope is the eval-entry compiler with no locals, so a free
    // name like `x` in `lambda y: x + y` must reach the eval namespace
    // through globals.  Otherwise LOAD_DEREF resolves to an empty cell
    // and the lambda silently treats `x` as None.  Mirrors the
    // compileFunctionDef logic added later in this file.
    std::unordered_set<std::string> bodyNonlocals;
    for (const auto& c : captured) {
        bool isParam = false;
        for (const auto& p : params) if (p == c) isParam = true;
        for (const auto& kw : n->kwonlyargs) if (kw == c) isParam = true;
        if (n->vararg == c) isParam = true;
        if (n->kwarg == c) isParam = true;
        if (!isParam) {
            bool isInOuterScope = localSlotMap_.count(c) || definedLocals_.count(c) || nonlocalNames_.count(c);
            if (isInOuterScope) bodyNonlocals.insert(c);
        }
    }

    const bool forceMapped = false; // Lambdas never have dynamic locals access like locals() or exec()
    std::vector<std::string> varnamesOrdered;
    // 1. Positional Parameters
    for (const auto& p : params) {
        varnamesOrdered.push_back(p);
    }
    // 2. Keyword-only Parameters
    for (const auto& kw : n->kwonlyargs) {
        bool alreadyIn = false;
        for (const auto& v : varnamesOrdered) if (v == kw) alreadyIn = true;
        if (!alreadyIn) varnamesOrdered.push_back(kw);
    }
    // 3. vararg and kwarg
    if (!n->vararg.empty()) {
        bool alreadyIn = false;
        for (const auto& v : varnamesOrdered) if (v == n->vararg) alreadyIn = true;
        if (!alreadyIn) varnamesOrdered.push_back(n->vararg);
    }
    if (!n->kwarg.empty()) {
        bool alreadyIn = false;
        for (const auto& v : varnamesOrdered) if (v == n->kwarg) alreadyIn = true;
        if (!alreadyIn) varnamesOrdered.push_back(n->kwarg);
    }
    
    std::unordered_map<std::string, int> slotMap;
    int automatic_count = 0;
    if (!forceMapped) {
        for (size_t i = 0; i < varnamesOrdered.size(); ++i) {
            slotMap[varnamesOrdered[i]] = static_cast<int>(i);
        }
        automatic_count = static_cast<int>(varnamesOrdered.size()); // finalized after body compilation
    }

    int nparams = static_cast<int>(params.size());
    int kwonlyargcount = static_cast<int>(n->kwonlyargs.size());

    Compiler bodyCompiler(ctx_, filename_);
    bodyCompiler.globalNames_ = globalNames_;
    for (const auto& g : bodyGlobals) bodyCompiler.globalNames_.insert(g);
    bodyCompiler.nonlocalNames_ = bodyNonlocals;
    bodyCompiler.localSlotMap_ = slotMap;
    for (const auto& v : varnamesOrdered) bodyCompiler.definedLocals_.insert(v);
    bodyCompiler.isFunctionScope_ = true;
    if (isClassBody_) bodyCompiler.currentClassName_ = currentClassName_;

    if (!bodyCompiler.compileNode(n->body.get())) return false;
    bodyCompiler.emit(OP_RETURN_VALUE);
    bodyCompiler.applyPatches();
    if (!forceMapped) automatic_count += bodyCompiler.getMaxStack() + 128;

    std::vector<const proto::ProtoObject*> varnamesVec;
    varnamesVec.reserve(varnamesOrdered.size());
    for (const auto& name : varnamesOrdered)
        varnamesVec.push_back(PythonEnvironment::getInternedString(ctx_, name.c_str())->asObject(ctx_));
    const proto::ProtoTuple* co_varnames = ctx_->newTuple(varnamesVec);
        
    int co_flags = CO_NEWLOCALS;
    if (!forceMapped) co_flags |= CO_OPTIMIZED;
    if (!captured.empty()) co_flags |= CO_NESTED;
    if (bodyCompiler.isGenerator_) co_flags |= 0x20; // CO_GENERATOR

    const proto::ProtoTuple* co_lnotab = bodyCompiler.getLnotab();

    const proto::ProtoObject* codeObj = makeCodeObject(ctx_, bodyCompiler.getConstants(), bodyCompiler.getNames(), bodyCompiler.getBytecode(), PythonEnvironment::getInternedString(ctx_, filename_.c_str()), co_varnames, nparams, kwonlyargcount, automatic_count, co_flags, bodyCompiler.isGenerator_, PythonEnvironment::getInternedString(ctx_, "<lambda>"), bodyCompiler.firstLine_, co_lnotab);
    if (!codeObj) return false;
    int idx = addConstant(codeObj);
    emit(OP_LOAD_CONST, idx);

    int make_fn_flags = 0;
    if (!n->defaults.empty()) {
        for (auto& d : n->defaults) {
            if (!compileNode(d.get())) return false;
        }
        emit(OP_BUILD_TUPLE, static_cast<int>(n->defaults.size()));
        make_fn_flags |= 0x01;
    }
    if (!n->kw_defaults.empty()) {
        int actual_count = 0;
        for (size_t i = 0; i < n->kw_defaults.size() && i < n->kwonlyargs.size(); ++i) {
            if (n->kw_defaults[i]) {
                const std::string& name = n->kwonlyargs[i];
                int nameIdx = addConstant(PythonEnvironment::getInternedString(ctx_, name.c_str())->asObject(ctx_));
                emit(OP_LOAD_CONST, nameIdx);
                if (!compileNode(n->kw_defaults[i].get())) return false;
                actual_count++;
            }
        }
        if (actual_count > 0) {
            emit(OP_BUILD_MAP, actual_count);
            make_fn_flags |= 0x02;
        }
    }
    emit(OP_BUILD_FUNCTION, make_fn_flags);
    
    return true;
}

bool Compiler::compileAsyncFunctionDef(AsyncFunctionDefNode* n) {
    if (!n) return false;
    std::unordered_set<std::string> bodyGlobals;
    std::unordered_set<std::string> bodyNonlocals;
    std::vector<std::string> localsOrdered;
    collectLocalsFromBody(n->body.get(), bodyGlobals, bodyNonlocals, localsOrdered);
    std::vector<std::string> params;
    if (!n->parameters.empty()) {
        for (const auto& p : n->parameters) params.push_back(p);
    }
    std::unordered_set<std::string> combinedGlobals = bodyGlobals;
    for (const auto& g : globalNames_) combinedGlobals.insert(g);

    std::unordered_set<std::string> captured;
    collectCapturedNames(n->body.get(), combinedGlobals, captured);
    // Also gather free variables of THIS body (mirrors compileFunctionDef's
    // self-free-vars pass).  Without this, names referenced directly in the
    // async body — not via a nested function — are not seen as candidates
    // for closure resolution.  This is benign on its own but pairs with the
    // outer-scope filter below.
    {
        std::unordered_set<std::string> bodyUsed;
        collectUsedNames(n->body.get(), bodyUsed);
        std::unordered_set<std::string> bodyDefined;
        collectDefinedNames(n->body.get(), bodyDefined);
        for (const auto& p : params) bodyDefined.insert(p);
        for (const auto& k : n->kwonlyargs) bodyDefined.insert(k);
        if (!n->vararg.empty()) bodyDefined.insert(n->vararg);
        if (!n->kwarg.empty()) bodyDefined.insert(n->kwarg);
        for (const auto& name : bodyUsed) {
            if (bodyDefined.count(name) || combinedGlobals.count(name) || bodyNonlocals.count(name))
                continue;
            captured.insert(name);
        }
    }
    // PE fix: only treat a captured name as nonlocal (LOAD_DEREF) when the
    // enclosing scope actually has it as a local or nonlocal — i.e. it can
    // be closed over via a cell.  Builtins and module-level globals must
    // resolve via LOAD_GLOBAL, not LOAD_DEREF, otherwise `print(...)` inside
    // an `async def` resolves to the wrong slot and TypeErrors at call.
    // This mirrors the logic in compileFunctionDef.
    for (const auto& c : captured) {
        bool isLocal = false;
        for (const auto& p : params) if (p == c) isLocal = true;
        for (const auto& kw : n->kwonlyargs) if (kw == c) isLocal = true;
        if (n->vararg == c) isLocal = true;
        if (n->kwarg == c) isLocal = true;
        for (const auto& l : localsOrdered) if (l == c) isLocal = true;
        if (!isLocal) {
            bool isInOuterScope = localSlotMap_.count(c) || definedLocals_.count(c) || nonlocalNames_.count(c);
            if (isInOuterScope) bodyNonlocals.insert(c);
        }
    }

    std::string dynamicReason = getDynamicLocalsReason(n->body.get());
    const bool forceMapped = !dynamicReason.empty();

    std::vector<std::string> varnamesOrdered;
    // 1. Positional Parameters
    for (const auto& p : params) {
        varnamesOrdered.push_back(p);
    }
    // 2. Keyword-only Parameters
    for (const auto& kw : n->kwonlyargs) {
        bool alreadyIn = false;
        for (const auto& v : varnamesOrdered) if (v == kw) alreadyIn = true;
        if (!alreadyIn) varnamesOrdered.push_back(kw);
    }
    // 3. vararg and kwarg
    if (!n->vararg.empty()) {
        bool alreadyIn = false;
        for (const auto& v : varnamesOrdered) if (v == n->vararg) alreadyIn = true;
        if (!alreadyIn) varnamesOrdered.push_back(n->vararg);
    }
    if (!n->kwarg.empty()) {
        bool alreadyIn = false;
        for (const auto& v : varnamesOrdered) if (v == n->kwarg) alreadyIn = true;
        if (!alreadyIn) varnamesOrdered.push_back(n->kwarg);
    }

    // 4. Then other locals
    for (const auto& loc : localsOrdered) {
        bool alreadyIn = false;
        for (const auto& v : varnamesOrdered) if (v == loc) alreadyIn = true;
        if (!alreadyIn) varnamesOrdered.push_back(loc);
    }

    std::unordered_map<std::string, int> slotMap;
    int automatic_count = 0;
    if (!forceMapped) {
        for (size_t i = 0; i < varnamesOrdered.size(); ++i) {
            slotMap[varnamesOrdered[i]] = static_cast<int>(i);
        }
        automatic_count = static_cast<int>(varnamesOrdered.size()); // finalized after body compilation
    }
    int nparams = static_cast<int>(params.size());
    int kwonlyargcount = static_cast<int>(n->kwonlyargs.size());

    Compiler bodyCompiler(ctx_, filename_);
    bodyCompiler.globalNames_ = globalNames_;
    for (const auto& g : bodyGlobals) bodyCompiler.globalNames_.insert(g);
    bodyCompiler.nonlocalNames_ = bodyNonlocals;
    bodyCompiler.localSlotMap_ = slotMap;
    bodyCompiler.isFunctionScope_ = true;
    bodyCompiler.isAsyncFunction_ = true;  // PC2
    if (!bodyCompiler.compileNode(n->body.get())) return false;

    PythonEnvironment* env = PythonEnvironment::fromContext(ctx_);
    int noneIdx = bodyCompiler.addConstant(env ? env->getNonePrototype() : PROTO_NONE);
    bodyCompiler.emit(OP_LOAD_CONST, noneIdx);
    bodyCompiler.emit(OP_RETURN_VALUE);

    bodyCompiler.applyPatches();
    if (!forceMapped) automatic_count += bodyCompiler.getMaxStack() + 128;

    std::vector<const proto::ProtoObject*> varnamesVec;
    varnamesVec.reserve(varnamesOrdered.size());
    for (const auto& name : varnamesOrdered)
        varnamesVec.push_back(PythonEnvironment::getInternedString(ctx_, name.c_str())->asObject(ctx_));
    const proto::ProtoTuple* co_varnames = ctx_->newTuple(varnamesVec);
    
    // CPython CO_COROUTINE = 0x100 (matches inspect.CO_COROUTINE)
    int co_flags = 256 | CO_NEWLOCALS;
    if (!forceMapped) co_flags |= CO_OPTIMIZED;
    if (!captured.empty()) co_flags |= CO_NESTED;
    if (!n->vararg.empty()) co_flags |= CO_VARARGS;
    if (!n->kwarg.empty()) co_flags |= CO_VARKEYWORDS;
    if (bodyCompiler.isGenerator_) co_flags |= 0x20; // CO_GENERATOR

    const proto::ProtoTuple* co_lnotab = bodyCompiler.getLnotab();

    const proto::ProtoObject* codeObj = makeCodeObject(ctx_, bodyCompiler.getConstants(), bodyCompiler.getNames(), bodyCompiler.getBytecode(), PythonEnvironment::getInternedString(ctx_, filename_.c_str()), co_varnames, nparams, kwonlyargcount, automatic_count, co_flags, bodyCompiler.isGenerator_, PythonEnvironment::getInternedString(ctx_, n->name.c_str()), bodyCompiler.firstLine_, co_lnotab);
    if (!codeObj) return false;
    int idx = addConstant(codeObj);
    emit(OP_LOAD_CONST, idx);

    int make_fn_flags = 0;
    if (!n->defaults.empty()) {
        for (auto& d : n->defaults) {
            if (!compileNode(d.get())) return false;
        }
        emit(OP_BUILD_TUPLE, static_cast<int>(n->defaults.size()));
        make_fn_flags |= 0x01;
    }
    if (!n->kw_defaults.empty()) {
        int actual_count = 0;
        for (size_t i = 0; i < n->kw_defaults.size() && i < n->kwonlyargs.size(); ++i) {
            if (n->kw_defaults[i]) {
                const std::string& name = n->kwonlyargs[i];
                int nameIdx = addConstant(PythonEnvironment::getInternedString(ctx_, name.c_str())->asObject(ctx_));
                emit(OP_LOAD_CONST, nameIdx);
                if (!compileNode(n->kw_defaults[i].get())) return false;
                actual_count++;
            }
        }
        if (actual_count > 0) {
            emit(OP_BUILD_MAP, actual_count);
            make_fn_flags |= 0x02;
        }
    }
    emit(OP_BUILD_FUNCTION, make_fn_flags);

    // Decorators (CPython 3.11+ CALL ABI: [NULL|self, callable, arg1..]).
    if (!n->decorator_list.empty()) {
        for (auto it = n->decorator_list.rbegin(); it != n->decorator_list.rend(); ++it) {
            emit(OP_PUSH_NULL, 0);
            emit(OP_ROT_TWO, 0);
            if (!compileNode(it->get())) return false;
            emit(OP_ROT_TWO, 0);
            emit(OP_CALL_FUNCTION, 1);
        }
    }

    return emitNameOp(n->name, TargetCtx::Store);
}

bool Compiler::compileAwait(AwaitNode* n) {
    if (!n || !compileNode(n->value.get())) return false;
    emit(OP_GET_AWAITABLE, 0);
    emit(OP_LOAD_CONST, addConstant(PROTO_NONE));
    emit(OP_YIELD_FROM, 0);
    return true;
}

bool Compiler::compileAsyncFor(AsyncForNode* n) {
    if (!n) return false;

    // PD3: lower `async for` to the sync FOR_ITER protocol.  The
    // async-iterator's __anext__ (PD2) returns the yielded value
    // synchronously and converts exhaustion to StopAsyncIteration.
    // We expose that same method as the iterator's __next__ at
    // runtime via the GET_AITER opcode (PD3-runtime), so the
    // existing FOR_ITER opcode just works.
    //
    // Bytecode emitted (mirrors compileFor):
    //   <iter expr>
    //   GET_AITER
    //   loopStart:
    //     FOR_ITER <jumpToEnd>
    //     STORE target
    //     <body>
    //     JUMP loopStart
    //   afterLoop:
    //     <orelse>

    if (!compileNode(n->iter.get())) return false;
    emit(OP_GET_AITER);

    int loopStart = bytecodeOffset();
    loopStack_.push_back({loopStart, {}, blockEnvStack_.size(), true});

    emit(OP_FOR_ITER, 0);
    int argSlot = bytecodeOffset() - 1;

    if (!compileTarget(n->target.get(), TargetCtx::Store)) return false;

    if (!compileNode(n->body.get())) return false;
    emit(OP_JUMP_ABSOLUTE, loopStart * 2);

    int afterLoop = bytecodeOffset();
    addPatch(argSlot, afterLoop);

    if (n->orelse) {
        if (!compileNode(n->orelse.get())) return false;
    }

    for (int patch : loopStack_.back().breakPatches) {
        addPatch(patch, bytecodeOffset());
    }
    loopStack_.pop_back();
    return true;
}

bool Compiler::compileAsyncWith(AsyncWithNode* n) {
    if (!n || n->items.empty()) return true;
    auto& item = n->items[0];

    // PF: rewritten compileAsyncWith.  The previous version had three
    // bugs that fully blocked async-with:
    //   (1) OP_LOAD_ATTR arg lacked the (idx<<1)|pushNull encoding,
    //       so names resolved to the wrong slot.
    //   (2) addPatch(slot + 1, ...) was off by one (mirrors the PE-2
    //       fix for compileAsyncFor).
    //   (3) Pre-fetching __aexit__ as a bound method onto the stack and
    //       trying to keep it there across the body interacts badly
    //       with the 3.11+ [NULL, callable, args] calling convention —
    //       the stale bound method is interpreted as a method-call
    //       receiver by CALL_FUNCTION on subsequent calls in the body.
    //
    // New strategy: keep only the *manager* on the stack across the
    // body, and re-fetch __aexit__ freshly at success and handler
    // paths.  This costs one extra method lookup per with-block but is
    // robust under the 3.11+ calling convention and avoids the
    // stack-juggling that produced the cross-talk.
    //
    // Bytecode layout for `async with mgr as v:`:
    //   <context_expr>                            → [..., m]
    //   DUP_TOP                                   → [..., m, m]
    //   LOAD_ATTR __aenter__ pushNull=1           → [..., m, NULL, enter_bound]
    //   CALL_FUNCTION 0                           → [..., m, awaitable]
    //   GET_AWAITABLE / LOAD_CONST None / YIELD_FROM
    //                                              → [..., m, enter_result]
    //   SETUP_FINALLY <handler>
    //   STORE target / POP_TOP                    → [..., m]
    //   <body>                                    → [..., m]
    //   POP_BLOCK
    //   DUP_TOP                                   → [..., m, m]
    //   LOAD_ATTR __aexit__ pushNull=1            → [..., m, NULL, exit_bound]
    //   LOAD_CONST None x3                        → [..., m, NULL, exit_bound, None, None, None]
    //   CALL_FUNCTION 3                           → [..., m, awaitable]
    //   GET_AWAITABLE / LOAD_CONST None / YIELD_FROM / POP_TOP
    //                                              → [..., m]
    //   POP_TOP                                   → [...]
    //   JUMP <after_handler>
    // handler: stack is [..., m, exc]   (PB-round invariant: pushed by SETUP_FINALLY)
    //   ROT_TWO                                   → [..., exc, m]
    //   DUP_TOP                                   → [..., exc, m, m]
    //   LOAD_ATTR __aexit__ pushNull=1            → [..., exc, m, NULL, exit_bound]
    //   <load type, exc, None as args, then call>
    //   POP_TOP / RAISE_VARARGS 0
    // after_handler:

    int noneIdx = addConstant(PROTO_NONE);
    int aenterIdx = addName("__aenter__");
    int aexitIdx  = addName("__aexit__");
    int classIdx  = addName("__class__");

    if (!compileNode(item.context_expr.get())) return false;
    // Stack: [..., m]

    emit(OP_DUP_TOP);
    // [..., m, m]

    emit(OP_LOAD_ATTR, (aenterIdx << 1) | 1);
    // [..., m, NULL, enter_bound]
    emit(OP_CALL_FUNCTION, 0);
    // [..., m, awaitable]
    emit(OP_GET_AWAITABLE);
    emit(OP_LOAD_CONST, noneIdx);
    emit(OP_YIELD_FROM);
    // [..., m, enter_result]

    if (item.optional_vars) {
        if (!compileTarget(item.optional_vars.get(), TargetCtx::Store)) return false;
    } else {
        emit(OP_POP_TOP);
    }
    // [..., m]

    // Register the cleanup handler AFTER STORE so the recorded
    // stackDepth is 1-above-outer (just `m`), keeping the exception
    // unwind stack predictable.  CPython's BEFORE_ASYNC_WITH /
    // SETUP_ASYNC_WITH similarly bracket only the body, not __aenter__.
    emit(OP_SETUP_FINALLY, 0);
    int setupFinallyArg = bytecodeOffset() - 1;

    if (!compileNode(n->body.get())) return false;
    // [..., m]

    emit(OP_POP_BLOCK);

    // Success: call m.__aexit__(None, None, None) and await
    emit(OP_DUP_TOP);
    // [..., m, m]
    emit(OP_LOAD_ATTR, (aexitIdx << 1) | 1);
    // [..., m, NULL, exit_bound]
    emit(OP_LOAD_CONST, noneIdx);
    emit(OP_LOAD_CONST, noneIdx);
    emit(OP_LOAD_CONST, noneIdx);
    emit(OP_CALL_FUNCTION, 3);
    // [..., m, awaitable]
    emit(OP_GET_AWAITABLE);
    emit(OP_LOAD_CONST, noneIdx);
    emit(OP_YIELD_FROM);
    // [..., m, exit_result]
    emit(OP_POP_TOP);
    // [..., m]
    emit(OP_POP_TOP);
    // [...]

    emit(OP_JUMP_ABSOLUTE, 0);
    int endJumpArg = bytecodeOffset() - 1;

    // Handler: protopy's exception unwinder pushes 3 items at handler
    // entry (traceback, value, "type" — actually just exc again).  Our
    // SETUP_FINALLY recorded stackDepth = 1 above outer (just `m`), so
    // the handler entry stack is [..., m, tb, exc, exc].  Normalise
    // down to [..., m, exc] before invoking the call sequence below.
    int handlerTarget = bytecodeOffset();
    addPatch(setupFinallyArg, handlerTarget);

    // [..., m, tb, exc, exc]
    emit(OP_POP_TOP);
    // [..., m, tb, exc]
    emit(OP_ROT_TWO);
    // [..., m, exc, tb]
    emit(OP_POP_TOP);
    // [..., m, exc]

    // Build args = (type(exc), exc, None) and leave it on top.
    // Stack: [..., m, exc]
    emit(OP_DUP_TOP);
    // [..., m, exc, exc]
    emit(OP_LOAD_ATTR, classIdx << 1);
    // [..., m, exc, type]
    emit(OP_ROT_TWO);
    // [..., m, type, exc]
    emit(OP_LOAD_CONST, noneIdx);
    // [..., m, type, exc, None]
    emit(OP_BUILD_TUPLE, 3);
    // [..., m, args]

    // Get bound exit method and assemble [NULL, exit_bound, args].
    emit(OP_ROT_TWO);
    // [..., args, m]
    emit(OP_LOAD_ATTR, aexitIdx << 1);
    // [..., args, exit_bound]
    emit(OP_ROT_TWO);
    // [..., exit_bound, args]
    emit(OP_PUSH_NULL);
    // [..., exit_bound, args, NULL]
    emit(OP_ROT_THREE);
    // [..., NULL, exit_bound, args]   (ROT_THREE: top NULL → bottom;
    //  middle exit_bound rotates up; orig 2nd args ends on top)
    emit(OP_CALL_FUNCTION_EX, 0);
    // [..., result]
    emit(OP_GET_AWAITABLE);
    emit(OP_LOAD_CONST, noneIdx);
    emit(OP_YIELD_FROM);
    // [..., aexit_result]

    // If aexit_result is truthy, suppress (jump to after_handler);
    // otherwise re-raise the pending exception via RAISE_VARARGS 0.
    emit(OP_POP_JUMP_IF_TRUE, 0);
    int suppressJumpArg = bytecodeOffset() - 1;
    emit(OP_RAISE_VARARGS, 0);

    int suppressTarget = bytecodeOffset();
    addPatch(suppressJumpArg, suppressTarget);

    int endTarget = bytecodeOffset();
    addPatch(endJumpArg, endTarget);

    return true;
}

bool Compiler::compileClassDef(ClassDefNode* n) {
    if (!n) return false;
    
    // 1. Name
    int nameIdx = addConstant(PythonEnvironment::getInternedString(ctx_, n->name.c_str())->asObject(ctx_));
    emit(OP_LOAD_CONST, nameIdx);
    
    // 2. Bases
    for (auto& b : n->bases) {
        if (!compileNode(b.get())) return false;
    }
    emit(OP_BUILD_TUPLE, static_cast<int>(n->bases.size()));
    
    // 2.5 Keywords
    if (get_env_diag()) {
        fprintf(stderr, "DEBUG Compiler: class '%s' keywords size=%zu\n", n->name.c_str(), n->keywords.size());
    }
    if (!n->keywords.empty()) {
        for (auto& kw : n->keywords) {
            int kIdx = addConstant(PythonEnvironment::getInternedString(ctx_, kw.first.c_str())->asObject(ctx_));
            emit(OP_LOAD_CONST, kIdx);
            if (!compileNode(kw.second.get())) return false;
        }
        emit(OP_BUILD_MAP, static_cast<int>(n->keywords.size()));
    } else {
        emit(OP_LOAD_CONST, addConstant(PROTO_NONE));
    }
    
    // 3. Body
    Compiler bodyCompiler(ctx_, filename_);
    bodyCompiler.globalNames_ = globalNames_;
    bodyCompiler.nonlocalNames_ = nonlocalNames_;
    bodyCompiler.isClassBody_ = true;
    bodyCompiler.currentClassName_ = n->name;
    // PI: compute and propagate __qualname__ prefix for nested classes.
    std::string thisQualname = qualnamePrefix_.empty()
        ? n->name
        : qualnamePrefix_ + "." + n->name;
    bodyCompiler.qualnamePrefix_ = thisQualname;
    // Emit __qualname__ at the start of the class body so BUILD_CLASS
    // can pick it up (overrides the default `__name__` qualname).
    {
        int qnameIdx = bodyCompiler.addConstant(
            PythonEnvironment::getInternedString(ctx_, thisQualname.c_str())->asObject(ctx_));
        bodyCompiler.emit(OP_LOAD_CONST, qnameIdx);
        bodyCompiler.emitNameOp("__qualname__", TargetCtx::Store);
    }

    // PH: implicitly capture the class's own name as a free variable
    // of the class body so methods can resolve zero-arg `super()`
    // (rewritten as `super(<ClassName>, self)`) via LOAD_DEREF.  The
    // outer-scope STORE_FAST that binds the class runs only after
    // BUILD_CLASS finishes, but BUILD_CLASS post-step writes the new
    // class into `ns` under the class's own name, so the closure walk
    // (innerCF.parent → ns) finds it.
    // classBodyFreevars accumulates every name this class body closes
    // over from an enclosing function scope; it is stamped onto the
    // body code object as `co_freevars` so OP_BUILD_FUNCTION snapshots
    // exactly these — not every enclosing local (which would leak an
    // enclosing `self` into a nested method's closure frame).
    std::unordered_set<std::string> classBodyFreevars;
    if (localSlotMap_.count(n->name) || definedLocals_.count(n->name) || nonlocalNames_.count(n->name)) {
        bodyCompiler.nonlocalNames_.insert(n->name);
        classBodyFreevars.insert(n->name);
    }

    // PG: capture free variables from the enclosing function scope.
    // Without this, references to enclosing locals (e.g. `x = D()`
    // where D is a class defined in the same function) fall through to
    // LOAD_NAME and fail because the class namespace dict, globals,
    // and builtins all lack the binding.
    //
    // The class body's free variables are (1) names used directly in
    // class-level statements plus (2) the free variables of nested
    // method/comprehension scopes.  collectUsedNames is *deep* — it
    // descends into nested function bodies and adds their parameters —
    // so using it raw treats a method parameter like `self` as a
    // class-body free var and makes the class wrongly capture the
    // enclosing function's `self`.  Use collectNestedScopeFreeVarsImpl
    // (which computes each nested scope's free vars against that
    // scope's own bindings) for (2), and a shallow per-statement scan
    // for (1) that does NOT descend into nested def/class bodies.
    {
        std::unordered_set<std::string> bodyUsed;
        collectNestedScopeFreeVarsImpl(n->body.get(), bodyUsed);
        if (auto* suite = dynamic_cast<SuiteNode*>(n->body.get())) {
            for (auto& st : suite->statements) {
                ASTNode* s = st.get();
                if (auto* fn = dynamic_cast<FunctionDefNode*>(s)) {
                    for (auto& d : fn->decorator_list) collectUsedNames(d.get(), bodyUsed);
                    for (auto& d : fn->defaults) collectUsedNames(d.get(), bodyUsed);
                    for (auto& d : fn->kw_defaults) if (d) collectUsedNames(d.get(), bodyUsed);
                } else if (auto* afn = dynamic_cast<AsyncFunctionDefNode*>(s)) {
                    for (auto& d : afn->decorator_list) collectUsedNames(d.get(), bodyUsed);
                    for (auto& d : afn->defaults) collectUsedNames(d.get(), bodyUsed);
                    for (auto& d : afn->kw_defaults) if (d) collectUsedNames(d.get(), bodyUsed);
                } else if (auto* cd = dynamic_cast<ClassDefNode*>(s)) {
                    for (auto& d : cd->decorator_list) collectUsedNames(d.get(), bodyUsed);
                    for (auto& b : cd->bases) collectUsedNames(b.get(), bodyUsed);
                    for (auto& kw : cd->keywords) if (kw.second) collectUsedNames(kw.second.get(), bodyUsed);
                } else {
                    collectUsedNames(s, bodyUsed);
                }
            }
        } else {
            collectUsedNames(n->body.get(), bodyUsed);
        }
        std::unordered_set<std::string> bodyDefined;
        collectDefinedNames(n->body.get(), bodyDefined);
        for (const auto& name : bodyUsed) {
            if (bodyDefined.count(name) || globalNames_.count(name) ||
                bodyCompiler.nonlocalNames_.count(name)) {
                continue;
            }
            // Only treat as nonlocal if the enclosing scope actually has
            // it as a local or nonlocal — i.e. it can be closed over via
            // a cell.  Names not in the outer scope are globals/builtins
            // and resolve via LOAD_NAME → globals → builtins as before.
            if (localSlotMap_.count(name) || definedLocals_.count(name) || nonlocalNames_.count(name)) {
                bodyCompiler.nonlocalNames_.insert(name);
                classBodyFreevars.insert(name);
            }
        }
    }

    // If the class body contains any annotations, initialise __annotations__ = {} first.
    bool hasAnnotations = false;
    if (auto* suite = dynamic_cast<SuiteNode*>(n->body.get())) {
        for (auto& stmt : suite->statements) {
            if (dynamic_cast<AnnAssignNode*>(stmt.get())) { hasAnnotations = true; break; }
        }
    }
    if (hasAnnotations) {
        bodyCompiler.emit(OP_BUILD_MAP, 0);
        bodyCompiler.emitNameOp("__annotations__", TargetCtx::Store);
    }

    if (!bodyCompiler.compileNode(n->body.get())) return false;
    bodyCompiler.emit(OP_RETURN_VALUE);
    bodyCompiler.applyPatches();
    
    const proto::ProtoObject* codeObj = makeCodeObject(ctx_,
        bodyCompiler.getConstants(), bodyCompiler.getNames(), bodyCompiler.getBytecode(),
        PythonEnvironment::getInternedString(ctx_, filename_.c_str()),
        nullptr, 0, 0, 0, 0, bodyCompiler.isGenerator_,
        PythonEnvironment::getInternedString(ctx_, n->name.c_str()),
        bodyCompiler.getFirstLine(), bodyCompiler.getLnotab());
    if (!codeObj) return false;
    // Stamp `co_freevars` so OP_BUILD_FUNCTION snapshots only the names
    // this class body actually closes over from enclosing function
    // scopes — not every enclosing local.
    {
        std::vector<const proto::ProtoObject*> freeVec;
        freeVec.reserve(classBodyFreevars.size());
        for (const auto& fv : classBodyFreevars)
            freeVec.push_back(PythonEnvironment::getInternedString(ctx_, fv.c_str())->asObject(ctx_));
        codeObj = codeObj->setAttribute(ctx_,
            PythonEnvironment::getInternedString(ctx_, "co_freevars"),
            ctx_->newTuple(freeVec)->asObject(ctx_));
    }
    int coIdx = addConstant(codeObj);
    emit(OP_LOAD_CONST, coIdx);
    emit(OP_BUILD_FUNCTION, 0);
    
    // 4. Build
    emit(OP_BUILD_CLASS);
    
    // Apply decorators Bottom-to-Top (CPython 3.11+ CALL ABI).
    if (!n->decorator_list.empty()) {
        for (auto it = n->decorator_list.rbegin(); it != n->decorator_list.rend(); ++it) {
            emit(OP_PUSH_NULL, 0);
            emit(OP_ROT_TWO, 0);
            if (!compileNode(it->get())) return false;
            emit(OP_ROT_TWO, 0);
            emit(OP_CALL_FUNCTION, 1);
        }
    }

    // 5. Store
    return emitNameOp(n->name, TargetCtx::Store);
}

bool Compiler::compileCondExpr(ConditionalExprNode* n) {
    if (!n) return false;
    if (!compileNode(n->test.get())) return false;
    emit(OP_POP_JUMP_IF_FALSE, 0);
    int elseSlot = bytecodeOffset() - 1;
    if (!compileNode(n->body.get())) return false;
    emit(OP_JUMP_ABSOLUTE, 0);
    int endSlot = bytecodeOffset() - 1;
    addPatch(elseSlot, bytecodeOffset());
    if (!compileNode(n->orelse.get())) return false;
    addPatch(endSlot, bytecodeOffset());
    return true;
}

bool Compiler::compileNode(ASTNode* node) {
    if (!node) return false;
    setLineNumber(node->line);
    bool result = false;
    if (dynamic_cast<PassNode*>(node)) result = true;
    else if (auto* fn = dynamic_cast<FunctionDefNode*>(node)) result = compileFunctionDef(fn);
    else if (auto* afn = dynamic_cast<AsyncFunctionDefNode*>(node)) result = compileAsyncFunctionDef(afn);
    else if (auto* aw = dynamic_cast<AwaitNode*>(node)) result = compileAwait(aw);
    else if (auto* afor = dynamic_cast<AsyncForNode*>(node)) result = compileAsyncFor(afor);
    else if (auto* awith = dynamic_cast<AsyncWithNode*>(node)) result = compileAsyncWith(awith);
    else if (auto* cl = dynamic_cast<ClassDefNode*>(node)) result = compileClassDef(cl);
    else if (auto* ce = dynamic_cast<ConditionalExprNode*>(node)) result = compileCondExpr(ce);
    else if (auto* c = dynamic_cast<ConstantNode*>(node)) result = compileConstant(c);
    else if (auto* nm = dynamic_cast<NameNode*>(node)) result = compileName(nm);
    else if (auto* b = dynamic_cast<BinOpNode*>(node)) result = compileBinOp(b);
    else if (auto* u = dynamic_cast<UnaryOpNode*>(node)) result = compileUnaryOp(u);
    else if (auto* cl = dynamic_cast<CallNode*>(node)) result = compileCall(cl);
    else if (auto* sn = dynamic_cast<StarredNode*>(node)) result = compileStarred(sn);
    else if (auto* att = dynamic_cast<AttributeNode*>(node)) result = compileAttribute(att);
    else if (auto* sub = dynamic_cast<SubscriptNode*>(node)) result = compileSubscript(sub);
    else if (auto* sl = dynamic_cast<SliceNode*>(node)) result = compileSlice(sl);
    else if (auto* lst = dynamic_cast<ListLiteralNode*>(node)) result = compileListLiteral(lst);
    else if (auto* d = dynamic_cast<DictLiteralNode*>(node)) result = compileDictLiteral(d);
    else if (auto* tup = dynamic_cast<TupleLiteralNode*>(node)) result = compileTupleLiteral(tup);
    else if (auto* set = dynamic_cast<SetLiteralNode*>(node)) result = compileSetLiteral(set);
    else if (auto* a = dynamic_cast<AssignNode*>(node)) result = compileAssign(a);
    else if (auto* aa = dynamic_cast<AnnAssignNode*>(node)) result = compileAnnAssign(aa);
    else if (auto* aa = dynamic_cast<AugAssignNode*>(node)) result = compileAugAssign(aa);
    else if (auto* an = dynamic_cast<AssertNode*>(node)) result = compileAssert(an);
    else if (auto* lc = dynamic_cast<ListCompNode*>(node)) result = compileListComp(lc);
    else if (auto* dc = dynamic_cast<DictCompNode*>(node)) result = compileDictComp(dc);
    else if (auto* sc = dynamic_cast<SetCompNode*>(node)) result = compileSetComp(sc);
    else if (auto* ge = dynamic_cast<GeneratorExpNode*>(node)) result = compileGeneratorExp(ge);
    else if (auto* lam = dynamic_cast<LambdaNode*>(node)) result = compileLambda(lam);
    else if (auto* js = dynamic_cast<JoinedStrNode*>(node)) result = compileJoinedStr(js);
    else if (auto* fv = dynamic_cast<FormattedValueNode*>(node)) result = compileFormattedValue(fv);
    else if (auto* d = dynamic_cast<DeleteNode*>(node)) result = compileDeleteNode(d);
    else if (auto* w = dynamic_cast<WhileNode*>(node)) result = compileWhile(w);
    else if (auto* f = dynamic_cast<ForNode*>(node)) result = compileFor(f);
    else if (auto* b = dynamic_cast<BreakNode*>(node)) result = compileBreak(b);
    else if (auto* c = dynamic_cast<ContinueNode*>(node)) result = compileContinue(c);
    else if (auto* iff = dynamic_cast<IfNode*>(node)) result = compileIf(iff);
    else if (auto* g = dynamic_cast<GlobalNode*>(node)) result = compileGlobal(g);
    else if (auto* nl = dynamic_cast<NonlocalNode*>(node)) result = compileNonlocal(nl);
    else if (auto* r = dynamic_cast<ReturnNode*>(node)) result = compileReturn(r);
    else if (auto* y = dynamic_cast<YieldNode*>(node)) result = compileYield(y);
    else if (auto* imp = dynamic_cast<ImportNode*>(node)) result = compileImport(imp);
    else if (auto* imf = dynamic_cast<ImportFromNode*>(node)) result = compileImportFrom(imf);
    else if (auto* t = dynamic_cast<TryNode*>(node)) result = compileTry(t);
    else if (auto* r = dynamic_cast<RaiseNode*>(node)) result = compileRaise(r);
    else if (auto* w = dynamic_cast<WithNode*>(node)) result = compileWith(w);
    else if (auto* s = dynamic_cast<SuiteNode*>(node)) result = compileSuite(s);
    else if (auto* ne = dynamic_cast<NamedExprNode*>(node)) result = compileNamedExpr(ne);
    else if (auto* ta = dynamic_cast<TypeAliasNode*>(node)) result = compileTypeAlias(ta);
    else if (auto* mn = dynamic_cast<MatchNode*>(node)) result = compileMatch(mn);

    if (!result && get_env_diag()) {
        std::cerr << "Compiler::compileNode FAILED for node type " << typeid(*node).name() << " at line " << node->line << "\n";
    }
    return result;
}

bool Compiler::compileExpression(ASTNode* expr) {
    if (!compileNode(expr)) return false;
    emit(OP_RETURN_VALUE, 0);
    applyPatches();
    return true;
}

const proto::ProtoTuple* Compiler::getLnotab() {
    std::vector<const proto::ProtoObject*> elems;
    elems.reserve(lnotabVec_.size());
    for (unsigned char b : lnotabVec_) {
        elems.push_back(ctx_->fromInteger(b));
    }
    return ctx_->newTuple(elems);
}

bool Compiler::compileModule(ModuleNode* mod) {
    if (!mod) return false;
    globalNames_.clear();

    // Only initialise __annotations__ at module load when the module
    // body actually contains a top-level annotated assignment.  CPython
    // pre-3.14 created the module __annotations__ on demand for the
    // first such statement; without that pre-existence the module
    // simply has no `__annotations__` attribute and a function reading
    // `__annotations__[k] = v` raises NameError (test_var_annot_in_module
    // covers exactly this).  Parenthesized targets `(x): T` do not count.
    auto hasTopLevelAnn = [](ASTNode* node) {
        if (auto* aa = dynamic_cast<AnnAssignNode*>(node)) {
            if (auto* nm = dynamic_cast<NameNode*>(aa->target.get())) {
                if (!nm->parenthesized) return true;
            }
        }
        return false;
    };
    bool anyAnn = false;
    for (auto& st : mod->body) {
        if (hasTopLevelAnn(st.get())) { anyAnn = true; break; }
    }
    if (anyAnn) {
        emit(OP_BUILD_MAP, 0);
        int idx = addName("__annotations__");
        emit(OP_STORE_NAME, (idx << 1));
    }

    for (size_t i = 0; i < mod->body.size(); ++i) {
        if (!compileNode(mod->body[i].get())) return false;
        if (statementLeavesValue(mod->body[i].get()))
            emit(OP_POP_TOP, 0);
    }

    // PEP 649 / 695: when the module body contained at least one
    // top-level annotated assignment, also expose `__annotate__` —
    // a callable that returns the populated `__annotations__` dict.
    // Test_var_annot_simple_exec asserts `lns["__annotate__"]` is
    // present after exec. Emitted at body-end so __annotations__ is
    // fully populated before being captured.
    if (anyAnn) {
        int annIdx = addName("__annotations__");
        int annotateIdx = addName("__annotate__");
        emit(OP_LOAD_NAME, (annIdx << 1));
        emit(OP_BUILD_ANNOTATE, 0);
        emit(OP_STORE_NAME, (annotateIdx << 1));
    }

    PythonEnvironment* env = PythonEnvironment::fromContext(ctx_);
    int noneIdx = addConstant(env ? env->getNonePrototype() : PROTO_NONE);
    emit(OP_LOAD_CONST, noneIdx);
    emit(OP_RETURN_VALUE);
    applyPatches();
    return true;
}

bool Compiler::unwindBlocks(bool isLoopExit, bool hasValueOnStack) {
    size_t targetDepth = 0;
    if (isLoopExit && !loopStack_.empty()) {
        targetDepth = loopStack_.back().blockDepth;
    }

    for (size_t i = blockEnvStack_.size(); i > targetDepth; --i) {
        // Access via index, NOT a stored reference: compileNode below may
        // recursively grow blockEnvStack_ and trigger a vector realloc
        // (which would invalidate any reference held across the call).
        size_t idx = i - 1;
        if (blockEnvStack_[idx].unwinding) {
            // We are already inlining this finally for an outer
            // break/continue/return. A nested transfer inside that
            // finally must NOT re-emit the same finally — silently
            // skip this entry (the outer pass will continue and
            // unwind the remaining outer entries).
            continue;
        }
        BlockType type = blockEnvStack_[idx].type;
        if (type == BlockType::TryFinally) {
            emit(OP_POP_BLOCK);
            ASTNode* cleanupNode = blockEnvStack_[idx].cleanupNode;
            if (cleanupNode) {
                blockEnvStack_[idx].unwinding = true;
                // hasValueOnStack=true ⇒ we're unwinding for `return`,
                // and the pending return value sits beneath whatever
                // the finally body builds.  Track the nesting depth so
                // any break/continue inside the recursively compiled
                // finally pops the pending value before redirecting.
                if (hasValueOnStack) returnUnwindDepth_++;
                bool ok = compileNode(cleanupNode);
                if (hasValueOnStack) returnUnwindDepth_--;
                // Re-index: vector may have been re-allocated, but the
                // entry at `idx` is still the same logical slot.
                blockEnvStack_[idx].unwinding = false;
                if (!ok) return false;
            }
        } else if (type == BlockType::With) {
            // Stack at this point:
            //   hasValueOnStack=false: [..., __exit__]
            //   hasValueOnStack=true:  [..., __exit__, retval]
            // OP_WITH_CLEANUP needs __exit__ on top, so swap when a return
            // value rides above it. After ROT_TWO the stack becomes
            // [..., retval, __exit__], and the cleanup consumes __exit__
            // and the None we push, leaving [..., retval] for the next
            // unwind iteration / RETURN_VALUE.
            if (hasValueOnStack) {
                emit(OP_ROT_TWO);
            }
            emit(OP_POP_BLOCK);
            int noneIdx = addConstant(PROTO_NONE);
            emit(OP_LOAD_CONST, noneIdx);
            emit(OP_WITH_CLEANUP);
            emit(OP_POP_TOP); // pop suppression flag
        }
    }
    return true;
}

const proto::ProtoObject* makeCodeObject(proto::ProtoContext* ctx,
    const proto::ProtoTuple* constants,
    const proto::ProtoTuple* names,
    const proto::ProtoTuple* bytecode,
    const proto::ProtoString* filename,
    const proto::ProtoTuple* varnames,
    int nparams,
    int kwonlyargcount,
    int automatic_count,
    int flags,
    bool isGenerator,
    const proto::ProtoString* co_name,
    int firstlineno,
    const proto::ProtoTuple* lnotab) {
    if (!ctx) return PROTO_NONE;
    PythonEnvironment* env = PythonEnvironment::get(ctx);
    const proto::ProtoObject* code = ctx->newObject(false);
    if (env && env->getCodePrototype()) {
        code = code->addParent(ctx, env->getCodePrototype());
    }
    code = code->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "co_consts"), constants ? reinterpret_cast<const proto::ProtoObject*>(constants) : reinterpret_cast<const proto::ProtoObject*>(ctx->newTuple()));
    code = code->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "co_names"), names ? reinterpret_cast<const proto::ProtoObject*>(names) : reinterpret_cast<const proto::ProtoObject*>(ctx->newTuple()));
    code = code->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "co_code"), reinterpret_cast<const proto::ProtoObject*>(bytecode));

    // Pre-compute a native int[] from the bytecode ProtoTuple once at compile time.
    // fromBuffer takes ownership of the heap allocation (freeOnExit=true); the GC calls
    // delete[] when the ByteBuffer Cell is collected, so lifetime matches the code object.
    // In executeBytecodeRange the int* is used directly, eliminating 16 AVL lookups +
    // 1 malloc/free per function call (paid on every recursive fib invocation in Step 5).
    if (bytecode && env && env->getCoNativeBytecodeString()) {
        const unsigned long bcSize = bytecode->getSize(ctx);
        if (bcSize > 0) {
            int* intBuf = new int[bcSize];
            for (unsigned long j = 0; j < bcSize; ++j) {
                const proto::ProtoObject* elem = bytecode->getAt(ctx, j);
                intBuf[j] = (elem && elem->isInteger(ctx)) ? static_cast<int>(elem->asLong(ctx)) : 0;
            }
            const proto::ProtoObject* nativeBcObj = ctx->fromBuffer(
                bcSize * sizeof(int), reinterpret_cast<char*>(intBuf), true);
            // Pin the ByteBuffer in moduleRoots so the GC never finalizes it while any
            // function using this code object is alive. Same pattern as getInternedString.
            if (ctx->space) {
                ctx->space->moduleRoots.push_back(nativeBcObj);
            }
            code = code->setAttribute(ctx, env->getCoNativeBytecodeString(), nativeBcObj);
        }
    }
    code = code->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "co_filename"), filename ? filename->asObject(ctx) : PythonEnvironment::getInternedString(ctx, "<stdin>")->asObject(ctx));
    code = code->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "co_varnames"), varnames ? reinterpret_cast<const proto::ProtoObject*>(varnames) : reinterpret_cast<const proto::ProtoObject*>(ctx->newTuple()));
    code = code->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "co_nparams"), ctx->fromInteger(nparams));
    code = code->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "co_kwonlyargcount"), ctx->fromInteger(kwonlyargcount));
    code = code->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "co_automatic_count"), ctx->fromInteger(automatic_count));
    code = code->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "co_flags"), ctx->fromInteger(flags));
    bool isGenOrCoro = isGenerator || (flags & 0x100);  // 0x100 = CO_COROUTINE
    code = code->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "co_is_generator"), ctx->fromBoolean(isGenOrCoro));
    code = code->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "co_name"), co_name ? co_name->asObject(ctx) : PythonEnvironment::getInternedString(ctx, "<module>")->asObject(ctx));
    
    code = code->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "co_firstlineno"), ctx->fromInteger(firstlineno));
    code = code->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "co_lnotab"), lnotab ? reinterpret_cast<const proto::ProtoObject*>(lnotab) : reinterpret_cast<const proto::ProtoObject*>(ctx->newTuple()));

    return code;
}

void Compiler::setLineNumber(int line) {
    if (line <= 0) return;
    if (firstLine_ == -1) {
        firstLine_ = line;
        lastLine_ = line;
        lastPC_ = 0;
        return;
    }
    
    int pc = bytecodeOffset();
    if (line == lastLine_) return;

    int pcDelta = pc - lastPC_;
    int lineDelta = line - lastLine_;

    if (pcDelta == 0 && lineDelta == 0) return;

    // Python lnotab format: (pc_offset, line_offset) pairs
    // We'll store it in a way that our updateContextLocation expects (pairs in a list, but lnotabVec_ is a helper here)
    // Actually, updateContextLocation expects a ProtoList of integers.
    // Let's just push to lnotabVec_ and then convert to ProtoList in getLnotab() or similar.
    
    // Simple implementation: 
    while (pcDelta > 255) {
        lnotabVec_.push_back(255);
        lnotabVec_.push_back(0);
        pcDelta -= 255;
    }
    // Note: lineDelta can be negative in CPython, but we'll assume forward for now if it's simpler, 
    // or handle negative if needed.
    // Our updateContextLocation handles line_offset as int.
    
    lnotabVec_.push_back(static_cast<unsigned char>(pcDelta));
    // Line delta can be larger than 127/255? 
    // Python uses signed char for line delta.
    if (lineDelta > 127) lineDelta = 127; 
    if (lineDelta < -128) lineDelta = -128;
    lnotabVec_.push_back(static_cast<unsigned char>(static_cast<signed char>(lineDelta)));

    lastPC_ = pc;
    lastLine_ = line;
}

namespace {
struct CodeObjectScope {
    CodeObjectScope(const proto::ProtoObject* code) : oldCode(PythonEnvironment::getCurrentCodeObject()) {
        PythonEnvironment::setCurrentCodeObject(code);
    }
    ~CodeObjectScope() {
        PythonEnvironment::setCurrentCodeObject(oldCode);
    }
    const proto::ProtoObject* oldCode;
};
}

const proto::ProtoObject* runCodeObject(proto::ProtoContext* ctx,
    const proto::ProtoObject* codeObj,
    proto::ProtoObject*& frame) {
    if (!ctx || !codeObj || !frame) {
        if (get_env_diag()) {
            fprintf(stderr, "DEBUG: runCodeObject early return: ctx=%p codeObj=%p frame=%p\n", (void*)ctx, (void*)codeObj, (void*)frame);
        }
        return PROTO_NONE;
    }
    
    if (get_env_diag()) {
        fprintf(stderr, "DEBUG: runCodeObject started\n");
    }
    
    CodeObjectScope cscope(codeObj);

    const proto::ProtoObject* co_consts = codeObj->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "co_consts"));
    const proto::ProtoObject* co_names = codeObj->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "co_names"));
    const proto::ProtoObject* co_code = codeObj->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "co_code"));
    const proto::ProtoObject* co_varnames = codeObj->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "co_varnames"));

    if (!co_consts || !co_consts->asTuple(ctx) || !co_code || !co_code->asTuple(ctx)) {
        if (get_env_diag()) {
            fprintf(stderr, "DEBUG: runCodeObject missing co_consts (%p) or co_code (%p)\n", (void*)co_consts, (void*)co_code);
        }
        return PROTO_NONE;
    }

    const proto::ProtoObject* co_automatic_obj = codeObj->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "co_automatic_count"));
    int automatic_count = (co_automatic_obj && co_automatic_obj->isInteger(ctx)) ? static_cast<int>(co_automatic_obj->asLong(ctx)) : 0;

    proto::ProtoContext* oldCtx = PythonEnvironment::getCurrentContext();

    // Always create a sub-context for code objects to ensure stack isolation,
    // especially for eval() and exec() calls which would otherwise clobber the caller's stack slots.
    const proto::ProtoList* localNames = ctx->newList();
    const proto::ProtoList* vlist = co_varnames ? co_varnames->asList(ctx) : nullptr;
    unsigned long vcount = vlist ? vlist->getSize(ctx) : 0;
    
    // Use the maximum of vcount and automatic_count to ensure enough slots are reserved for both locals and stack.
    int nLocalsNeeded = std::max((int)vcount, (int)automatic_count);
    for (int i = 0; i < nLocalsNeeded; ++i) {
        const proto::ProtoObject* name = (vlist && i < (int)vcount) ? vlist->getAt(ctx, i) : PROTO_NONE;
        localNames = localNames->appendLast(ctx, name);
    }
    
    proto::ProtoContext* subCtx = new proto::ProtoContext(ctx->space, ctx, nullptr, localNames, nullptr, nullptr);
    proto::ProtoContext* execCtx = subCtx;
    PythonEnvironment::setCurrentContext(execCtx);

    unsigned long stackOffset = (co_varnames && co_varnames->asTuple(execCtx)) ? co_varnames->asTuple(execCtx)->getSize(execCtx) : 0;

    if (get_env_diag()) {
        fprintf(stderr, "DEBUG: runCodeObject co_code size=%lu co_consts size=%lu stackOffset=%lu\n",
            co_code->asTuple(execCtx)->getSize(execCtx),
            co_consts->asTuple(execCtx)->getSize(execCtx),
            stackOffset);
    }

    const proto::ProtoObject* result = executeBytecodeRange(execCtx, co_consts->asTuple(execCtx), co_code->asTuple(execCtx),
        co_names ? co_names->asTuple(execCtx) : nullptr, frame, 0, co_code->asTuple(execCtx)->getSize(execCtx),
        stackOffset);

    if (get_env_diag()) {
        fprintf(stderr, "DEBUG: runCodeObject result=%p\n", (void*)result);
    }

    if (subCtx) {
        PythonEnvironment::setCurrentContext(oldCtx);
        subCtx->returnValue = result;
        delete subCtx;
    }
    return result;
}


bool Compiler::compileJoinedStr(JoinedStrNode* n) {
    if (!n) return false;
    for (const auto& val : n->values) {
        if (!compileNode(val.get())) return false;
    }
    emit(OP_BUILD_STRING, static_cast<int>(n->values.size()));
    return true;
}

bool Compiler::compileFormattedValue(FormattedValueNode* n) {
    if (!n) return false;
    // Resolve repr/str/ascii through emitNameOp so the load picks the right
    // opcode (LOAD_FAST / LOAD_GLOBAL / LOAD_NAME) for the current scope.
    // Previously this site emitted OP_LOAD_NAME unconditionally, which inside
    // a function body bypassed env->resolve fast-path and could surface a
    // stray PROTO_NONE from the frame's prototype chain — manifesting as
    // 'NoneType' object is not callable for f"{x!r}" / !s / !a inside a
    // function.  emitNameOp also injects the OP_PUSH_NULL ahead of the load
    // for the 3.11+ calling convention, so we must not emit it here.
    const char* convName = nullptr;
    if (n->conversion == 'r')      convName = "repr";
    else if (n->conversion == 's') convName = "str";
    else if (n->conversion == 'a') convName = "ascii";
    // PEP 498: when a format spec is present, render via the format()
    // builtin so the f-string and `'{:spec}'.format(x)` share one code
    // path.  Layout the stack as [.., NULL, format, value, spec] before
    // OP_CALL_FUNCTION 2.  Otherwise leave the bare (possibly
    // converted) value on the stack — the surrounding OP_BUILD_STRING
    // handles the str() coercion at join time.
    if (!n->format_spec.empty()) {
        if (!emitNameOp("format", TargetCtx::Load, /*pushNull=*/true)) return false;
        if (convName) {
            if (!emitNameOp(convName, TargetCtx::Load, /*pushNull=*/true)) return false;
            if (!compileNode(n->value.get())) return false;
            emit(OP_CALL_FUNCTION, 1);
        } else {
            if (!compileNode(n->value.get())) return false;
        }
        int specIdx = addConstant(PythonEnvironment::getInternedString(
            ctx_, n->format_spec.c_str())->asObject(ctx_));
        emit(OP_LOAD_CONST, specIdx);
        emit(OP_CALL_FUNCTION, 2);
        return true;
    }
    if (convName) {
        if (!emitNameOp(convName, TargetCtx::Load, /*pushNull=*/true)) return false;
        if (!compileNode(n->value.get())) return false;
        emit(OP_CALL_FUNCTION, 1);
    } else {
        if (!compileNode(n->value.get())) return false;
    }
    return true;
}

bool Compiler::compileNonlocal(NonlocalNode* n) {
    // Nonlocals are handled in collectLocalsFromBody / emitNameOp
    return true;
}

bool Compiler::compileTypeAlias(TypeAliasNode* n) {
    if (!n) return false;
    // PEP 695: type Alias[T] = Value.
    // For now, we compile the value and store it to the name.
    if (!compileNode(n->value.get())) return false;
    return emitNameOp(n->name, TargetCtx::Store);
}

// =========================================================================
// match/case (PEP 634)
// =========================================================================
//
// All pattern-test code uses the same convention:
//
//   compilePattern(pat, failLabelSlot, collectedNames):
//       Pre:  TOS = candidate value to be matched.
//       Post (success): stack returns to its pre-state (TOS consumed);
//                       binding names introduced by the pattern are
//                       stored.
//       Post (fail):    a JUMP_ABSOLUTE to failLabelSlot is taken with
//                       the stack restored to its pre-state.
//
// The implementation stashes the candidate into a fresh `__match_tmp_<n>`
// local at entry and accesses it via LOAD_NAME from then on. This avoids
// the stack-juggling that would otherwise be needed to keep the candidate
// reachable for sub-pattern tests, isinstance checks, and capture binding.

void Compiler::collectPatternNames(const MatchPatternNode* pat,
    std::vector<std::string>& out) {
    if (!pat) return;
    if (auto* a = dynamic_cast<const MatchAsPatternNode*>(pat)) {
        if (!a->name.empty() && a->name != "_") out.push_back(a->name);
        if (a->pattern) collectPatternNames(a->pattern.get(), out);
        return;
    }
    if (auto* o = dynamic_cast<const MatchOrPatternNode*>(pat)) {
        // PEP 634 requires every alt to bind the same names.  Collect
        // from the first alt; the validity check is enforced at compile.
        if (!o->alternatives.empty()) {
            collectPatternNames(o->alternatives.front().get(), out);
        }
        return;
    }
    if (auto* s = dynamic_cast<const MatchSequencePatternNode*>(pat)) {
        for (auto& p : s->patterns) collectPatternNames(p.get(), out);
        return;
    }
    if (auto* st = dynamic_cast<const MatchStarPatternNode*>(pat)) {
        if (!st->name.empty()) out.push_back(st->name);
        return;
    }
    if (auto* m = dynamic_cast<const MatchMappingPatternNode*>(pat)) {
        for (auto& p : m->patterns) collectPatternNames(p.get(), out);
        if (!m->rest.empty()) out.push_back(m->rest);
        return;
    }
    if (auto* c = dynamic_cast<const MatchClassPatternNode*>(pat)) {
        for (auto& p : c->args) collectPatternNames(p.get(), out);
        for (auto& kv : c->kwargs) collectPatternNames(kv.second.get(), out);
        return;
    }
    // MatchValue, MatchSingleton: no bindings.
}

bool Compiler::compilePattern(MatchPatternNode* pat, int failLabelSlot,
    std::vector<std::string>* collectedNames) {
    if (!pat) return false;

    // ---- MatchSingleton: None / True / False -----------------------------
    if (auto* s = dynamic_cast<MatchSingletonPatternNode*>(pat)) {
        // TOS = candidate.  Compare with `is`.
        const proto::ProtoObject* obj =
            (s->kind == MatchSingletonPatternNode::Kind::None_) ? PROTO_NONE :
            (s->kind == MatchSingletonPatternNode::Kind::True_) ? PROTO_TRUE :
                                                                  PROTO_FALSE;
        int idx = addConstant(obj);
        emit(OP_LOAD_CONST, idx);
        emit(OP_COMPARE_OP, 8); // 'is'
        emit(OP_POP_JUMP_IF_FALSE, 0);
        addPatch(bytecodeOffset() - 1, failLabelSlot);
        return true;
    }

    // ---- MatchValue: literal or dotted attribute -------------------------
    if (auto* v = dynamic_cast<MatchValuePatternNode*>(pat)) {
        // TOS = candidate.  Push value, COMPARE_OP ==, jump if false.
        if (!compileNode(v->value.get())) return false;
        emit(OP_COMPARE_OP, 0); // '=='
        emit(OP_POP_JUMP_IF_FALSE, 0);
        addPatch(bytecodeOffset() - 1, failLabelSlot);
        return true;
    }

    // ---- MatchAs / Capture / Wildcard ------------------------------------
    if (auto* a = dynamic_cast<MatchAsPatternNode*>(pat)) {
        if (!a->pattern) {
            // Bare capture or wildcard.
            if (a->name == "_" || a->name.empty()) {
                emit(OP_POP_TOP);
            } else {
                emitNameOp(a->name, TargetCtx::Store);
            }
            return true;
        }
        // <pat> as name: stash, run sub-pattern on a copy, then bind.
        std::string tmp = "__match_tmp_" + std::to_string(matchTmpCounter_++);
        emitNameOp(tmp, TargetCtx::Store); // consume candidate, save in tmp
        emitNameOp(tmp, TargetCtx::Load);  // push for sub-pattern
        if (!compilePattern(a->pattern.get(), failLabelSlot, collectedNames)) return false;
        // Sub-pattern matched — bind name from the saved tmp.
        if (!a->name.empty() && a->name != "_") {
            emitNameOp(tmp, TargetCtx::Load);
            emitNameOp(a->name, TargetCtx::Store);
        }
        return true;
    }

    // ---- MatchOr ---------------------------------------------------------
    if (auto* o = dynamic_cast<MatchOrPatternNode*>(pat)) {
        // Stash candidate in tmp; for each alt, reload and try; on success
        // jump to the shared success label. Failure of alt i (i < last)
        // falls through to alt i+1; failure of the last alt jumps to the
        // outer failLabelSlot.
        size_t n = o->alternatives.size();
        if (n == 0) {
            emit(OP_POP_TOP);
            emit(OP_JUMP_ABSOLUTE, 0);
            addPatch(bytecodeOffset() - 1, failLabelSlot);
            return true;
        }
        std::string tmp = "__match_tmp_" + std::to_string(matchTmpCounter_++);
        emitNameOp(tmp, TargetCtx::Store);
        std::vector<int> successJumps;
        // For each alt we use a per-alt fail trampoline (an
        // OP_JUMP_ABSOLUTE placed at alt-start, jumped over by an
        // OP_JUMP_ABSOLUTE that targets the alt body). The trampoline's
        // target gets patched to either the next-alt entry or the outer
        // fail label.
        for (size_t i = 0; i < n; ++i) {
            // Skip-trampoline.
            emit(OP_JUMP_ABSOLUTE, 0);
            int skipSlot = bytecodeOffset() - 1;
            // Trampoline (target patched at end of iteration).
            emit(OP_JUMP_ABSOLUTE, 0);
            int trampolineSlot = bytecodeOffset() - 1;
            // Skip-target = right after trampoline.
            addPatch(skipSlot, bytecodeOffset());
            // Body of this alt: reload candidate from tmp, then compile alt.
            emitNameOp(tmp, TargetCtx::Load);
            std::vector<std::string> altNames;
            if (!compilePattern(o->alternatives[i].get(), trampolineSlot, &altNames)) return false;
            // Success — jump to the shared success label (patched at end).
            emit(OP_JUMP_ABSOLUTE, 0);
            successJumps.push_back(bytecodeOffset() - 1);
            // Patch the trampoline. Last alt → outer fail; otherwise → next iter.
            if (i + 1 == n) {
                addPatch(trampolineSlot, failLabelSlot);
            } else {
                addPatch(trampolineSlot, bytecodeOffset()); // start of next alt's skip-trampoline
            }
        }
        // success label = current bytecode offset; patch all successJumps.
        int succEntry = bytecodeOffset();
        for (int slot : successJumps) addPatch(slot, succEntry);
        return true;
    }

    // ---- MatchClass ------------------------------------------------------
    if (auto* c = dynamic_cast<MatchClassPatternNode*>(pat)) {
        // Stash candidate into tmp.
        std::string tmp = "__match_tmp_" + std::to_string(matchTmpCounter_++);
        emitNameOp(tmp, TargetCtx::Store);
        // Test isinstance(tmp, cls)
        emitNameOp(std::string("isinstance"), TargetCtx::Load);
        emitNameOp(tmp, TargetCtx::Load);
        if (!compileNode(c->cls.get())) return false;
        emit(OP_CALL_FUNCTION, 2);
        emit(OP_POP_JUMP_IF_FALSE, 0);
        addPatch(bytecodeOffset() - 1, failLabelSlot);
        // Positional arg patterns: use cls.__match_args__[i] for the attr name.
        if (!c->args.empty()) {
            // For each positional, fetch attr by name from __match_args__.
            // Stash the cls expression result in a tmp so we evaluate once.
            std::string clsTmp = "__match_cls_" + std::to_string(matchTmpCounter_++);
            if (!compileNode(c->cls.get())) return false;
            emitNameOp(clsTmp, TargetCtx::Store);
            for (size_t i = 0; i < c->args.size(); ++i) {
                // getattr(tmp, clsTmp.__match_args__[i])
                emitNameOp(std::string("getattr"), TargetCtx::Load);
                emitNameOp(tmp, TargetCtx::Load);
                emitNameOp(clsTmp, TargetCtx::Load);
                {
                    int n = addName(std::string("__match_args__"));
                    emit(OP_LOAD_ATTR, n << 1);
                }
                emit(OP_LOAD_CONST, addConstant(ctx_->fromInteger(static_cast<long long>(i))));
                emit(OP_BINARY_SUBSCR);
                emit(OP_CALL_FUNCTION, 2);
                if (!compilePattern(c->args[i].get(), failLabelSlot, collectedNames)) return false;
            }
        }
        // Keyword arg patterns: cls.kw == ... resolved via attr access.
        for (auto& kv : c->kwargs) {
            emitNameOp(tmp, TargetCtx::Load);
            int n = addName(kv.first);
            emit(OP_LOAD_ATTR, n << 1);
            if (!compilePattern(kv.second.get(), failLabelSlot, collectedNames)) return false;
        }
        return true;
    }

    // ---- MatchSequence ---------------------------------------------------
    if (auto* sq = dynamic_cast<MatchSequencePatternNode*>(pat)) {
        std::string tmp = "__match_tmp_" + std::to_string(matchTmpCounter_++);
        emitNameOp(tmp, TargetCtx::Store);
        // PEP 634: strings, bytes, and bytearrays are NOT considered
        // sequences for matching purposes.  Reject them explicitly first.
        emitNameOp(std::string("isinstance"), TargetCtx::Load);
        emitNameOp(tmp, TargetCtx::Load);
        emitNameOp(std::string("str"), TargetCtx::Load);
        emitNameOp(std::string("bytes"), TargetCtx::Load);
        emitNameOp(std::string("bytearray"), TargetCtx::Load);
        emit(OP_BUILD_TUPLE, 3);
        emit(OP_CALL_FUNCTION, 2);
        emit(OP_POP_JUMP_IF_TRUE, 0);
        addPatch(bytecodeOffset() - 1, failLabelSlot);
        // Then accept list/tuple.  PEP 634 prescribes "Sequence" via
        // collections.abc; the concrete builtins cover the common cases
        // (custom Sequence subclasses can be matched with MatchClass).
        emitNameOp(std::string("isinstance"), TargetCtx::Load);
        emitNameOp(tmp, TargetCtx::Load);
        emitNameOp(std::string("list"), TargetCtx::Load);
        emitNameOp(std::string("tuple"), TargetCtx::Load);
        emit(OP_BUILD_TUPLE, 2);
        emit(OP_CALL_FUNCTION, 2);
        emit(OP_POP_JUMP_IF_FALSE, 0);
        addPatch(bytecodeOffset() - 1, failLabelSlot);
        // Find a star pattern (at most one).
        int starIdx = -1;
        for (size_t i = 0; i < sq->patterns.size(); ++i) {
            if (dynamic_cast<MatchStarPatternNode*>(sq->patterns[i].get())) {
                if (starIdx >= 0) {
                    return false; // multiple stars — invalid
                }
                starIdx = static_cast<int>(i);
            }
        }
        int totalFixed = static_cast<int>(sq->patterns.size()) - (starIdx >= 0 ? 1 : 0);
        // len(tmp) == totalFixed (no star)  or  len(tmp) >= totalFixed (with star)
        emitNameOp(std::string("len"), TargetCtx::Load);
        emitNameOp(tmp, TargetCtx::Load);
        emit(OP_CALL_FUNCTION, 1);
        emit(OP_LOAD_CONST, addConstant(ctx_->fromInteger(totalFixed)));
        emit(OP_COMPARE_OP, starIdx >= 0 ? 5 /* >= */ : 0 /* == */);
        emit(OP_POP_JUMP_IF_FALSE, 0);
        addPatch(bytecodeOffset() - 1, failLabelSlot);
        // Fixed prefix patterns [0..starIdx) (or [0..N) if no star).
        int prefixEnd = (starIdx >= 0) ? starIdx : static_cast<int>(sq->patterns.size());
        for (int i = 0; i < prefixEnd; ++i) {
            emitNameOp(tmp, TargetCtx::Load);
            emit(OP_LOAD_CONST, addConstant(ctx_->fromInteger(i)));
            emit(OP_BINARY_SUBSCR);
            if (!compilePattern(sq->patterns[i].get(), failLabelSlot, collectedNames)) return false;
        }
        // Star binding (and capture if named).
        if (starIdx >= 0) {
            auto* star = dynamic_cast<MatchStarPatternNode*>(sq->patterns[starIdx].get());
            int suffixCount = static_cast<int>(sq->patterns.size()) - starIdx - 1;
            // tmp[starIdx : len(tmp) - suffixCount]  via slice.
            emitNameOp(tmp, TargetCtx::Load);
            emit(OP_LOAD_CONST, addConstant(ctx_->fromInteger(starIdx)));
            // stop = -suffixCount, or len(tmp) if suffixCount == 0
            if (suffixCount == 0) {
                int idxNone = addConstant(PROTO_NONE);
                emit(OP_LOAD_CONST, idxNone);
            } else {
                emit(OP_LOAD_CONST, addConstant(ctx_->fromInteger(-suffixCount)));
            }
            emit(OP_BUILD_SLICE, 2);
            emit(OP_BINARY_SUBSCR);
            // Make the star binding a list (PEP 634 says so).
            emitNameOp(std::string("list"), TargetCtx::Load);
            emit(OP_ROT_TWO);
            emit(OP_CALL_FUNCTION, 1);
            if (!star->name.empty()) {
                emitNameOp(star->name, TargetCtx::Store);
            } else {
                emit(OP_POP_TOP);
            }
            // Suffix patterns (negative indices from end).
            for (int i = starIdx + 1; i < static_cast<int>(sq->patterns.size()); ++i) {
                int negIdx = -(static_cast<int>(sq->patterns.size()) - i);
                emitNameOp(tmp, TargetCtx::Load);
                emit(OP_LOAD_CONST, addConstant(ctx_->fromInteger(negIdx)));
                emit(OP_BINARY_SUBSCR);
                if (!compilePattern(sq->patterns[i].get(), failLabelSlot, collectedNames)) return false;
            }
        }
        return true;
    }

    // ---- MatchMapping ----------------------------------------------------
    if (auto* mp = dynamic_cast<MatchMappingPatternNode*>(pat)) {
        std::string tmp = "__match_tmp_" + std::to_string(matchTmpCounter_++);
        emitNameOp(tmp, TargetCtx::Store);
        // isinstance(tmp, dict)
        emitNameOp(std::string("isinstance"), TargetCtx::Load);
        emitNameOp(tmp, TargetCtx::Load);
        emitNameOp(std::string("dict"), TargetCtx::Load);
        emit(OP_CALL_FUNCTION, 2);
        emit(OP_POP_JUMP_IF_FALSE, 0);
        addPatch(bytecodeOffset() - 1, failLabelSlot);
        // For each (key, sub-pattern): test `key in tmp`, then load and recurse.
        for (size_t i = 0; i < mp->keys.size(); ++i) {
            // key in tmp
            if (!compileNode(mp->keys[i].get())) return false;
            emitNameOp(tmp, TargetCtx::Load);
            emit(OP_COMPARE_OP, 6); // 'in'
            emit(OP_POP_JUMP_IF_FALSE, 0);
            addPatch(bytecodeOffset() - 1, failLabelSlot);
            // tmp[key]
            emitNameOp(tmp, TargetCtx::Load);
            if (!compileNode(mp->keys[i].get())) return false;
            emit(OP_BINARY_SUBSCR);
            if (!compilePattern(mp->patterns[i].get(), failLabelSlot, collectedNames)) return false;
        }
        // **rest binding: build a dict copy minus the matched keys.
        if (!mp->rest.empty()) {
            emitNameOp(std::string("dict"), TargetCtx::Load);
            emitNameOp(tmp, TargetCtx::Load);
            emit(OP_CALL_FUNCTION, 1);
            // Stash so we can DELETE_SUBSCR in a loop.
            std::string restTmp = "__match_rest_" + std::to_string(matchTmpCounter_++);
            emitNameOp(restTmp, TargetCtx::Store);
            for (auto& k : mp->keys) {
                // del restTmp[k]
                emitNameOp(restTmp, TargetCtx::Load);
                if (!compileNode(k.get())) return false;
                emit(OP_DELETE_SUBSCR);
            }
            emitNameOp(restTmp, TargetCtx::Load);
            emitNameOp(mp->rest, TargetCtx::Store);
        }
        return true;
    }

    // ---- MatchStarPatternNode (only valid inside MatchSequence) ---------
    if (dynamic_cast<MatchStarPatternNode*>(pat)) {
        // Reaching here is a parser/compiler error — handled inside
        // compileSequencePattern.
        return false;
    }

    return false;
}

bool Compiler::compileMatch(MatchNode* n) {
    if (!n || !n->subject) return false;
    matchTmpCounter_ = 0;

    // Stash subject in a top-level tmp once; each case loads it via tmp.
    if (!compileNode(n->subject.get())) return false;
    std::string subjTmp = "__match_subj";
    emitNameOp(subjTmp, TargetCtx::Store);

    std::vector<int> endJumps; // success jumps from each case body to the end

    for (auto& mc : n->cases) {
        // Per-case fail label: we'll patch all pattern failures here.
        // The pattern emits OP_JUMP_ABSOLUTE arg=0 with addPatch(slot, X);
        // we need a single slot index for failLabelSlot. Allocate via
        // a trampoline: emit a placeholder JUMP_ABSOLUTE here that we patch
        // to point to the body, and have pattern failures patch to after
        // the body.  Cleanest: make the pattern's "fail target" a TRAMPOLINE
        // emitted just before the next case, then patch the trampoline
        // forward to the next case's start.
        //
        // Implementation: emit a "skip-trampoline" that jumps over the
        // trampoline so the success path doesn't fall into it.  Pattern
        // failures jump to the trampoline; the trampoline's arg gets
        // patched to the next case's start (or to the post-match end).
        //
        // Layout:
        //
        //   <load subject from subjTmp>                ←─ caseStart
        //   <pattern bytecode>
        //   <if guard: compile guard, POP_JUMP_IF_FALSE to caseFailJump>
        //   <body>
        //   JUMP_ABSOLUTE end                              (recorded)
        // caseFailJump:
        //   JUMP_ABSOLUTE next_case_or_end                 (single trampoline)
        //
        // Pattern failures and guard failure both target caseFailJump's slot.

        // Load subject for this case.
        emitNameOp(subjTmp, TargetCtx::Load);

        // Reserve a forward slot: emit a trampoline-jump that we'll patch
        // to next-case-start.  Place it AFTER the case body — so we need
        // forward references.  Trick: emit a "skip" jump first, then the
        // trampoline, then the body.  Pattern fails jump backward to the
        // trampoline.
        emit(OP_JUMP_ABSOLUTE, 0); // skip-over-trampoline
        int skipSlot = bytecodeOffset() - 1;
        emit(OP_JUMP_ABSOLUTE, 0); // trampoline (target = next case)
        int trampolineSlot = bytecodeOffset() - 1;
        // Patch skip to land RIGHT AFTER the trampoline.
        addPatch(skipSlot, bytecodeOffset());

        // Compile pattern with failLabel = trampolineSlot.
        std::vector<std::string> caseNames;
        if (!compilePattern(mc->pattern.get(), trampolineSlot, &caseNames)) return false;

        // Optional guard.
        if (mc->guard) {
            if (!compileNode(mc->guard.get())) return false;
            emit(OP_POP_JUMP_IF_FALSE, 0);
            addPatch(bytecodeOffset() - 1, trampolineSlot);
        }

        // Body.
        if (!compileNode(mc->body.get())) return false;

        // Jump to the end.
        emit(OP_JUMP_ABSOLUTE, 0);
        endJumps.push_back(bytecodeOffset() - 1);

        // The trampoline now points to here (next-case-start).
        addPatch(trampolineSlot, bytecodeOffset());
    }

    // No case matched — fall through to end (silently).
    int endHere = bytecodeOffset();
    for (int slot : endJumps) addPatch(slot, endHere);
    return true;
}

} // namespace protoPython
