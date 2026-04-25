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
}

int Compiler::addConstant(const proto::ProtoObject* obj) {
    if (obj == PROTO_NONE || obj == PROTO_TRUE || obj == PROTO_FALSE) {
        int n = static_cast<int>(constantsVec_.size());
        for (int i = 0; i < n; ++i) {
            if (constantsVec_[i] == obj) return i;
        }
        constantsVec_.push_back(obj);
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
            int idx = static_cast<int>(constantsVec_.size());
            constantsVec_.push_back(obj);
            constIntIndex_[val] = idx;
            return idx;
        } catch (const std::overflow_error&) {
            // Bignum — no numeric cache.
            int n = static_cast<int>(constantsVec_.size());
            for (int i = 0; i < n; ++i) {
                if (constantsVec_[i] == obj) return i;
            }
            constantsVec_.push_back(obj);
            return n;
        }
    }
    if (obj->isDouble(ctx_)) {
        double val = obj->asDouble(ctx_);
        auto it = constFloatIndex_.find(val);
        if (it != constFloatIndex_.end()) return it->second;
        int idx = static_cast<int>(constantsVec_.size());
        constantsVec_.push_back(obj);
        constFloatIndex_[val] = idx;
        return idx;
    }
    if (obj->isString(ctx_)) {
        std::string val;
        obj->asString(ctx_)->toUTF8String(ctx_, val);
        auto it = constStrIndex_.find(val);
        if (it != constStrIndex_.end()) return it->second;
        int idx = static_cast<int>(constantsVec_.size());
        constantsVec_.push_back(obj);
        constStrIndex_[val] = idx;
        return idx;
    }

    // Fallback for other objects (should be rare for constants)
    int n = static_cast<int>(constantsVec_.size());
    for (int i = 0; i < n; ++i) {
        if (constantsVec_[i] == obj) return i;
    }
    int idx = n;
    constantsVec_.push_back(obj);
    return idx;
}

int Compiler::addName(const std::string& name) {
    auto it = namesIndex_.find(name);
    if (it != namesIndex_.end()) return it->second;
    int idx = static_cast<int>(namesVec_.size());
    auto* env = protoPython::PythonEnvironment::get(ctx_);
    const proto::ProtoObject* str = env ? reinterpret_cast<const proto::ProtoObject*>(env->getInternedString(ctx_, name.c_str())) : proto::ProtoString::createSymbol(ctx_, name.c_str())->asObject(ctx_);
    namesVec_.push_back(str);
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
            return 1;
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
        fprintf(stderr, "COMPILING [%p]: offset=%zu op=%d arg=%d\n", (void*)this, bytecodeVec_.size(), op, arg);
    }
    bytecodeVec_.push_back(ctx_->fromInteger(op));
    bytecodeVec_.push_back(ctx_->fromInteger(arg));
    currentStack_ += stackEffect(op, arg);
    if (currentStack_ > maxStack_) maxStack_ = currentStack_;
}

int Compiler::bytecodeOffset() const {
    return static_cast<int>(bytecodeVec_.size()) / 2;
}

void Compiler::addPatch(int argSlotIndex, int targetBytecodeIndex) {
    patches_.emplace_back(argSlotIndex, targetBytecodeIndex);
}

void Compiler::applyPatches() {
    for (const auto& p : patches_) {
        // p.first is the instruction index (bytecodeOffset() value)
        // the arg slot is at index (p.first * 2) + 1 in the bytecodeVec_
        unsigned long arrayIdx = static_cast<unsigned long>(p.first) * 2 + 1;
        if (arrayIdx < bytecodeVec_.size())
            bytecodeVec_[arrayIdx] = ctx_->fromInteger(p.second * 2); // ExecutionEngine jumps to array index!
    }
    patches_.clear();
}

const proto::ProtoTuple* Compiler::getConstants() {
    if (!constants_) {
        constants_ = ctx_->newTuple(constantsVec_);
    }
    return constants_;
}

const proto::ProtoTuple* Compiler::getNames() {
    if (!names_) {
        names_ = ctx_->newTuple(namesVec_);
    }
    return names_;
}

const proto::ProtoTuple* Compiler::getBytecode() {
    if (!bytecode_) {
        bytecode_ = ctx_->newTuple(bytecodeVec_);
    }
    return bytecode_;
}

bool Compiler::compileConstant(ConstantNode* n) {
    if (!n) return false;
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
    else if (n->constType == ConstantNode::ConstType::Str)
        obj = PythonEnvironment::getInternedString(ctx_, n->strVal.c_str())->asObject(ctx_);
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
    } else if (n->op == TokenType::Is) {
        emit(OP_COMPARE_OP, 8); // 8 is 'is'
        return true;
    } else if (n->op == TokenType::IsNot) {
        emit(OP_COMPARE_OP, 9); // 9 is 'is not'
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
    // Rewrite to super(__class__, self) where __class__ is the enclosing class name (global lookup)
    // and self is the first parameter (LOAD_FAST 0). This mirrors CPython's __classcell__ mechanism
    // without requiring cell variables.
    if (isFunctionScope_ && !currentClassName_.empty() && n->args.empty() && n->keywords.empty()) {
        if (auto* nameN = dynamic_cast<NameNode*>(n->func.get())) {
            if (nameN->id == "super") {
                int superIdx = addName("super");
                emit(OP_LOAD_GLOBAL, (superIdx << 1) | 1);   // NULL marker + super
                int classIdx = addName(currentClassName_);
                emit(OP_LOAD_GLOBAL, classIdx << 1);          // the defining class
                emit(OP_LOAD_FAST, 0);                        // self (first parameter)
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
                    int nameIdx = addConstant(PythonEnvironment::getInternedString(ctx_, kw.first.c_str())->asObject(ctx_));
                    emit(OP_LOAD_CONST, nameIdx);
                    if (!compileNode(kw.second.get())) return false;
                    emit(OP_MAP_ADD, 1);
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
    int idx = addName(n->attr);
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

bool Compiler::compileListLiteral(ListLiteralNode* n) {
    if (!n) return false;
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
                if (!compileNode(n->keys[i].get()) || !compileNode(n->values[i].get())) return false;
                emit(OP_MAP_ADD, 1);
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

bool Compiler::emitNameOp(const std::string& id, TargetCtx ctx, bool pushNull) {
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
        int idx = addName(att->attr);
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
            int keyIdx = addConstant(PythonEnvironment::getInternedString(ctx_, nm->id.c_str())->asObject(ctx_));
            emit(OP_LOAD_CONST, keyIdx);                           // key (TOS)
            emit(OP_STORE_SUBSCR);
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
    if (!n || !compileNode(n->test.get())) return false;
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
    if (!unwindBlocks(false)) return false;
    emit(OP_RETURN_VALUE);
    return true;
}

bool Compiler::compileYield(YieldNode* n) {
    // Same rule as `return`: `yield` / `yield from` are only legal inside
    // a function body (which, once we see a yield, becomes a generator).
    if (!isFunctionScope_ || isClassBody_) {
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
            if (localSlotMap_.count(name) || nonlocalNames_.count(name)) {
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

    // Create the list inside the function
    bodyCompiler.emit(OP_BUILD_LIST, 0);
    
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
    
    bodyCompiler.emit(OP_RETURN_VALUE);
    bodyCompiler.applyPatches();
    
    // Create code object
    std::vector<const proto::ProtoObject*> varnamesVec;
    varnamesVec.reserve(orderedLocals.size());
    for (const auto& name : orderedLocals)
        varnamesVec.push_back(PythonEnvironment::getInternedString(ctx_, name.c_str())->asObject(ctx_));
    const proto::ProtoTuple* co_varnames = ctx_->newTuple(varnamesVec);
    
    int flags = CO_OPTIMIZED | CO_NEWLOCALS;
    if (isAsync) flags |= 128; // CO_COROUTINE

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
            if (localSlotMap_.count(name) || nonlocalNames_.count(name)) {
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
    if (isAsync) flags |= 128; // CO_COROUTINE

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
            if (localSlotMap_.count(name) || nonlocalNames_.count(name)) {
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
    if (isAsync) flags |= 128; // CO_COROUTINE

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
            if (localSlotMap_.count(name) || nonlocalNames_.count(name)) {
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
    if (isAsync) flags |= 128; // CO_COROUTINE

    const proto::ProtoObject* codeObj = makeCodeObject(ctx_, 
        bodyCompiler.getConstants(), bodyCompiler.getNames(), bodyCompiler.getBytecode(), 
        PythonEnvironment::getInternedString(ctx_, filename_.c_str()), 
        co_varnames, 1, 0, static_cast<int>(orderedLocals.size()) + bodyCompiler.getMaxStack() + 32,
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
    if (auto* iff = dynamic_cast<IfNode*>(node)) {
        collectUsedNames(iff->test.get(), out);
        collectUsedNames(iff->body.get(), out);
        if (iff->orelse) collectUsedNames(iff->orelse.get(), out);
        return;
    }
    if (auto* fn = dynamic_cast<FunctionDefNode*>(node)) {
        for (const auto& p : fn->parameters) out.insert(p);
        collectUsedNames(fn->body.get(), out);
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
        if (auto* nm = dynamic_cast<NameNode*>(aa->target.get())) {
            out.insert(nm->id);
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

/** Returns non-empty string if body has dynamic locals access; reason for slot fallback. */
static std::string getDynamicLocalsReason(ASTNode* node) {
    if (!node) return "";
    if (auto* call = dynamic_cast<CallNode*>(node)) {
        if (call->func) {
            if (auto* nm = dynamic_cast<NameNode*>(call->func.get())) {
                if (nm->id == "locals") return "locals";
                if (nm->id == "exec") return "exec";
                if (nm->id == "eval") return "eval";
            }
        }
    }
    if (auto* suite = dynamic_cast<SuiteNode*>(node)) {
        for (auto& st : suite->statements) {
            std::string r = getDynamicLocalsReason(st.get());
            if (!r.empty()) return r;
        }
        return "";
    }
    if (auto* iff = dynamic_cast<IfNode*>(node)) {
        std::string r = getDynamicLocalsReason(iff->body.get());
        if (!r.empty()) return r;
        if (iff->orelse) return getDynamicLocalsReason(iff->orelse.get());
        return "";
    }
    if (auto* fn = dynamic_cast<FunctionDefNode*>(node)) {
        return getDynamicLocalsReason(fn->body.get());
    }
    if (auto* f = dynamic_cast<ForNode*>(node)) {
        return getDynamicLocalsReason(f->body.get());
    }
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

bool Compiler::compileFunctionDef(FunctionDefNode* n) {
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
            bool isInOuterScope = localSlotMap_.count(c) || nonlocalNames_.count(c);
            if (isInOuterScope) bodyNonlocals.insert(c);
        }
    }

    std::string dynamicReason = getDynamicLocalsReason(n->body.get());
    const bool forceMapped = !dynamicReason.empty();

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
    bodyCompiler.isFunctionScope_ = true;
    bodyCompiler.forceMapped_ = forceMapped;
    // Only propagate currentClassName_ when this function is a direct class method
    // (i.e., defined inside a class body). Nested functions inside methods do not
    // inherit the class name to avoid false super() rewrites.
    if (isClassBody_) bodyCompiler.currentClassName_ = currentClassName_;
    if (!bodyCompiler.compileNode(n->body.get())) return false;
    if (!forceMapped) automatic_count += bodyCompiler.getMaxStack() + 32;

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
    
    // Apply decorators Bottom-to-Top
    if (!n->decorator_list.empty()) {
        for (auto it = n->decorator_list.rbegin(); it != n->decorator_list.rend(); ++it) {
            if (!compileNode(it->get())) return false;
            emit(OP_ROT_TWO, 0);
            emit(OP_CALL_FUNCTION, 1);
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
    
    std::unordered_set<std::string> bodyNonlocals;
    for (const auto& c : captured) {
        bool isParam = false;
        for (const auto& p : params) if (p == c) isParam = true;
        for (const auto& kw : n->kwonlyargs) if (kw == c) isParam = true;
        if (n->vararg == c) isParam = true;
        if (n->kwarg == c) isParam = true;
        if (!isParam) bodyNonlocals.insert(c);
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
    bodyCompiler.isFunctionScope_ = true;
    if (isClassBody_) bodyCompiler.currentClassName_ = currentClassName_;

    if (!bodyCompiler.compileNode(n->body.get())) return false;
    bodyCompiler.emit(OP_RETURN_VALUE);
    bodyCompiler.applyPatches();
    if (!forceMapped) automatic_count += bodyCompiler.getMaxStack() + 32;

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
    for (const auto& c : captured) {
        bool isLocal = false;
        for (const auto& p : params) if (p == c) isLocal = true;
        for (const auto& kw : n->kwonlyargs) if (kw == c) isLocal = true;
        if (n->vararg == c) isLocal = true;
        if (n->kwarg == c) isLocal = true;
        for (const auto& l : localsOrdered) if (l == c) isLocal = true;
        if (!isLocal) bodyNonlocals.insert(c);
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
    if (!bodyCompiler.compileNode(n->body.get())) return false;

    PythonEnvironment* env = PythonEnvironment::fromContext(ctx_);
    int noneIdx = bodyCompiler.addConstant(env ? env->getNonePrototype() : PROTO_NONE);
    bodyCompiler.emit(OP_LOAD_CONST, noneIdx);
    bodyCompiler.emit(OP_RETURN_VALUE);

    bodyCompiler.applyPatches();
    if (!forceMapped) automatic_count += bodyCompiler.getMaxStack() + 32;

    std::vector<const proto::ProtoObject*> varnamesVec;
    varnamesVec.reserve(varnamesOrdered.size());
    for (const auto& name : varnamesOrdered)
        varnamesVec.push_back(PythonEnvironment::getInternedString(ctx_, name.c_str())->asObject(ctx_));
    const proto::ProtoTuple* co_varnames = ctx_->newTuple(varnamesVec);
    
    // 0x80 is CO_COROUTINE
    int co_flags = 128 | CO_NEWLOCALS; 
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
    
    if (!n->decorator_list.empty()) {
        for (auto it = n->decorator_list.rbegin(); it != n->decorator_list.rend(); ++it) {
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
    
    // 1. iter = GET_AITER(n->iter)
    if (!compileNode(n->iter.get())) return false;
    emit(OP_GET_AITER);

    int loopStart = bytecodeOffset();
    loopStack_.push_back({loopStart, {}, blockEnvStack_.size(), true});

    // 2. SETUP_FINALLY to catch StopAsyncIteration
    int setupFinallySlot = bytecodeOffset();
    emit(OP_SETUP_FINALLY, 0);

    // 3. val = await anext(iter)
    emit(OP_GET_ANEXT);
    emit(OP_GET_AWAITABLE);
    emit(OP_LOAD_CONST, addConstant(PROTO_NONE));
    emit(OP_YIELD_FROM);

    // 4. Success: pop block and store
    emit(OP_POP_BLOCK);
    if (!compileTarget(n->target.get(), TargetCtx::Store)) return false;

    // 5. Body
    if (!compileNode(n->body.get())) return false;
    emit(OP_JUMP_ABSOLUTE, loopStart * 2);

    // 6. Handler (StopAsyncIteration)
    int handlerTarget = bytecodeOffset();
    addPatch(setupFinallySlot + 1, handlerTarget);

    int idx = addName("StopAsyncIteration");
    emit(OP_LOAD_GLOBAL, (idx << 1) | 1);
    emit(OP_EXCEPTION_MATCH);
    
    int popJumpSlot = bytecodeOffset();
    emit(OP_POP_JUMP_IF_FALSE, 0);

    // If matches StopAsyncIteration:
    emit(OP_POP_TOP); // pop exception
    emit(OP_POP_TOP); // pop value
    emit(OP_POP_TOP); // pop traceback
    emit(OP_POP_EXCEPT);
    
    int endJumpSlot = bytecodeOffset();
    emit(OP_JUMP_FORWARD, 0); // Skip re-raise
    
    // If NOT StopAsyncIteration
    int notStopAsyncSlot = bytecodeOffset();
    addPatch(popJumpSlot + 1, notStopAsyncSlot);
    emit(OP_RERAISE, 0);

    // After loop (normal termination branch)
    int afterLoop = bytecodeOffset();
    addPatch(endJumpSlot + 1, afterLoop);
    
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

    // 1. context_manager = context_expr
    if (!compileNode(item.context_expr.get())) return false;
    emit(OP_DUP_TOP);
    
    // 2. exit = context_manager.__aexit__
    emit(OP_LOAD_ATTR, addName("__aexit__"));
    emit(OP_ROT_TWO); // [..., exit, manager]
    
    // 3. enter_res = await context_manager.__aenter__()
    emit(OP_LOAD_ATTR, addName("__aenter__"));
    emit(OP_CALL_FUNCTION, 0);
    emit(OP_GET_AWAITABLE);
    emit(OP_LOAD_CONST, addConstant(PROTO_NONE));
    emit(OP_YIELD_FROM);
    
    // [..., exit, enter_res]
    
    // 4. SETUP_FINALLY to catch exceptions in body
    int setupFinallySlot = bytecodeOffset();
    emit(OP_SETUP_FINALLY, 0);

    // 5. Store enter_res in target
    if (item.optional_vars) {
        if (!compileTarget(item.optional_vars.get(), TargetCtx::Store)) return false;
    } else {
        emit(OP_POP_TOP);
    }
    
    // 6. Body
    if (!compileNode(n->body.get())) return false;
    
    // 7. Success: POP_BLOCK and call exit(None, None, None)
    emit(OP_POP_BLOCK);
    
    emit(OP_LOAD_CONST, addConstant(PROTO_NONE));
    emit(OP_LOAD_CONST, addConstant(PROTO_NONE));
    emit(OP_LOAD_CONST, addConstant(PROTO_NONE));
    emit(OP_CALL_FUNCTION, 3);
    emit(OP_GET_AWAITABLE);
    emit(OP_LOAD_CONST, addConstant(PROTO_NONE));
    emit(OP_YIELD_FROM);
    emit(OP_POP_TOP);
    
    int endJumpSlot = bytecodeOffset();
    emit(OP_JUMP_ABSOLUTE, 0);

    // 8. Handler: call exit(type, exc, None) and reraise
    int handlerTarget = bytecodeOffset();
    addPatch(setupFinallySlot + 1, handlerTarget);
    
    // [..., exit, exc]
    emit(OP_DUP_TOP); // exc
    emit(OP_LOAD_ATTR, addName("__class__")); // type
    emit(OP_ROT_TWO); // [..., type, exc]
    emit(OP_LOAD_CONST, addConstant(PROTO_NONE)); // [..., type, exc, None]
    
    // Stack is now [..., exit, type, exc, None]
    emit(OP_CALL_FUNCTION, 3);
    emit(OP_GET_AWAITABLE);
    emit(OP_LOAD_CONST, addConstant(PROTO_NONE));
    emit(OP_YIELD_FROM);
    emit(OP_POP_TOP); // discard exit result
    
    // Reraise original exception (we need to have kept it)
    // For now we just raise the last one or allow propagation if we didn't clear it.
    // ExecutionEngine cleared it. So we need to raise it again.
    emit(OP_RAISE_VARARGS, 1);

    int endTarget = bytecodeOffset();
    addPatch(endJumpSlot + 1, endTarget);

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
    int coIdx = addConstant(codeObj);
    emit(OP_LOAD_CONST, coIdx);
    emit(OP_BUILD_FUNCTION, 0);
    
    // 4. Build
    emit(OP_BUILD_CLASS);
    
    // Apply decorators Bottom-to-Top
    if (!n->decorator_list.empty()) {
        for (auto it = n->decorator_list.rbegin(); it != n->decorator_list.rend(); ++it) {
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
    
    for (size_t i = 0; i < mod->body.size(); ++i) {
        if (!compileNode(mod->body[i].get())) return false;
        if (statementLeavesValue(mod->body[i].get()))
            emit(OP_POP_TOP, 0);
    }
    
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx_);
    int noneIdx = addConstant(env ? env->getNonePrototype() : PROTO_NONE);
    emit(OP_LOAD_CONST, noneIdx);
    emit(OP_RETURN_VALUE);
    applyPatches();
    return true;
}

bool Compiler::unwindBlocks(bool isLoopExit) {
    size_t targetDepth = 0;
    if (isLoopExit && !loopStack_.empty()) {
        targetDepth = loopStack_.back().blockDepth;
    }
    
    for (size_t i = blockEnvStack_.size(); i > targetDepth; --i) {
        const BlockEnv& env = blockEnvStack_[i - 1];
        if (env.type == BlockType::TryFinally) {
            emit(OP_POP_BLOCK);
            if (env.cleanupNode) {
                if (!compileNode(env.cleanupNode)) return false;
            }
        } else if (env.type == BlockType::With) {
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
    bool isGenOrCoro = isGenerator || (flags & 0x80);
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
    if (n->conversion == 'r') {
        // Use 3.11+ calling convention: [NULL, callable, arg]
        emit(OP_PUSH_NULL);
        emit(OP_LOAD_NAME, (addName("repr") << 1));
        if (!compileNode(n->value.get())) return false;
        emit(OP_CALL_FUNCTION, 1);
    } else if (n->conversion == 's') {
        emit(OP_PUSH_NULL);
        emit(OP_LOAD_NAME, (addName("str") << 1));
        if (!compileNode(n->value.get())) return false;
        emit(OP_CALL_FUNCTION, 1);
    } else if (n->conversion == 'a') {
        emit(OP_PUSH_NULL);
        emit(OP_LOAD_NAME, (addName("ascii") << 1));
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

} // namespace protoPython
