// The bytecode disassembler behind `protopy --disasm` (include/protoPython/
// Disassembler.h, docs/VM.md).  It reads code objects through their public
// attributes (co_code, co_consts, co_names, co_varnames, ...) and never runs
// any of their code.

#include <protoPython/Disassembler.h>
#include <protoPython/Compiler.h>
#include <protoPython/ExecutionEngine.h>
#include <protoPython/Parser.h>
#include <protoPython/PythonEnvironment.h>

#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace protoPython {

const char* opcodeName(int op) {
    switch (op) {
#define PROTOPY_OPNAME(name) case OP_##name: return #name;
        PROTOPY_OPNAME(LOAD_CONST) PROTOPY_OPNAME(RETURN_VALUE) PROTOPY_OPNAME(LOAD_NAME)
        PROTOPY_OPNAME(STORE_NAME) PROTOPY_OPNAME(BINARY_ADD) PROTOPY_OPNAME(BINARY_SUBTRACT)
        PROTOPY_OPNAME(CALL_FUNCTION) PROTOPY_OPNAME(BINARY_MULTIPLY) PROTOPY_OPNAME(BINARY_TRUE_DIVIDE)
        PROTOPY_OPNAME(COMPARE_OP) PROTOPY_OPNAME(POP_JUMP_IF_FALSE) PROTOPY_OPNAME(JUMP_ABSOLUTE)
        PROTOPY_OPNAME(LOAD_ATTR) PROTOPY_OPNAME(STORE_ATTR) PROTOPY_OPNAME(BUILD_LIST)
        PROTOPY_OPNAME(BINARY_SUBSCR) PROTOPY_OPNAME(BUILD_MAP) PROTOPY_OPNAME(STORE_SUBSCR)
        PROTOPY_OPNAME(BUILD_TUPLE) PROTOPY_OPNAME(GET_ITER) PROTOPY_OPNAME(FOR_ITER)
        PROTOPY_OPNAME(UNPACK_SEQUENCE) PROTOPY_OPNAME(LOAD_GLOBAL) PROTOPY_OPNAME(STORE_GLOBAL)
        PROTOPY_OPNAME(BUILD_SLICE) PROTOPY_OPNAME(ROT_TWO) PROTOPY_OPNAME(DUP_TOP)
        PROTOPY_OPNAME(BINARY_MODULO) PROTOPY_OPNAME(BINARY_POWER) PROTOPY_OPNAME(BINARY_FLOOR_DIVIDE)
        PROTOPY_OPNAME(UNARY_NEGATIVE) PROTOPY_OPNAME(UNARY_NOT) PROTOPY_OPNAME(UNARY_INVERT)
        PROTOPY_OPNAME(POP_TOP) PROTOPY_OPNAME(UNARY_POSITIVE) PROTOPY_OPNAME(NOP)
        PROTOPY_OPNAME(INPLACE_ADD) PROTOPY_OPNAME(BINARY_LSHIFT) PROTOPY_OPNAME(BINARY_RSHIFT)
        PROTOPY_OPNAME(INPLACE_SUBTRACT) PROTOPY_OPNAME(BINARY_AND) PROTOPY_OPNAME(BINARY_OR)
        PROTOPY_OPNAME(BINARY_XOR) PROTOPY_OPNAME(INPLACE_MULTIPLY) PROTOPY_OPNAME(INPLACE_TRUE_DIVIDE)
        PROTOPY_OPNAME(INPLACE_FLOOR_DIVIDE) PROTOPY_OPNAME(INPLACE_MODULO) PROTOPY_OPNAME(INPLACE_POWER)
        PROTOPY_OPNAME(INPLACE_LSHIFT) PROTOPY_OPNAME(INPLACE_RSHIFT) PROTOPY_OPNAME(INPLACE_AND)
        PROTOPY_OPNAME(INPLACE_OR) PROTOPY_OPNAME(INPLACE_XOR) PROTOPY_OPNAME(ROT_THREE)
        PROTOPY_OPNAME(ROT_FOUR) PROTOPY_OPNAME(DUP_TOP_TWO) PROTOPY_OPNAME(BUILD_FUNCTION)
        PROTOPY_OPNAME(LOAD_FAST) PROTOPY_OPNAME(STORE_FAST) PROTOPY_OPNAME(CALL_FUNCTION_KW)
        PROTOPY_OPNAME(BUILD_CLASS) PROTOPY_OPNAME(DELETE_NAME) PROTOPY_OPNAME(DELETE_ATTR)
        PROTOPY_OPNAME(DELETE_SUBSCR) PROTOPY_OPNAME(DELETE_GLOBAL) PROTOPY_OPNAME(DELETE_FAST)
        PROTOPY_OPNAME(RAISE_VARARGS) PROTOPY_OPNAME(LIST_APPEND) PROTOPY_OPNAME(MAP_ADD)
        PROTOPY_OPNAME(SET_ADD) PROTOPY_OPNAME(BUILD_SET) PROTOPY_OPNAME(YIELD_VALUE)
        PROTOPY_OPNAME(SETUP_WITH) PROTOPY_OPNAME(WITH_CLEANUP) PROTOPY_OPNAME(GET_YIELD_FROM_ITER)
        PROTOPY_OPNAME(YIELD_FROM) PROTOPY_OPNAME(SETUP_FINALLY) PROTOPY_OPNAME(POP_BLOCK)
        PROTOPY_OPNAME(BUILD_STRING) PROTOPY_OPNAME(LOAD_DEREF) PROTOPY_OPNAME(STORE_DEREF)
        PROTOPY_OPNAME(CALL_FUNCTION_EX) PROTOPY_OPNAME(LIST_EXTEND) PROTOPY_OPNAME(DICT_UPDATE)
        PROTOPY_OPNAME(SET_UPDATE) PROTOPY_OPNAME(LIST_TO_TUPLE) PROTOPY_OPNAME(GET_AWAITABLE)
        PROTOPY_OPNAME(GET_AITER) PROTOPY_OPNAME(GET_ANEXT) PROTOPY_OPNAME(EXCEPTION_MATCH)
        PROTOPY_OPNAME(SETUP_ASYNC_WITH) PROTOPY_OPNAME(BINARY_MATRIX_MULTIPLY)
        PROTOPY_OPNAME(INPLACE_MATRIX_MULTIPLY) PROTOPY_OPNAME(RERAISE) PROTOPY_OPNAME(JUMP_FORWARD)
        PROTOPY_OPNAME(FORMAT_VALUE) PROTOPY_OPNAME(GEN_START) PROTOPY_OPNAME(GET_LEN)
        PROTOPY_OPNAME(MATCH_MAPPING) PROTOPY_OPNAME(MATCH_SEQUENCE) PROTOPY_OPNAME(EXTENDED_ARG)
        PROTOPY_OPNAME(POP_EXCEPT) PROTOPY_OPNAME(IMPORT_STAR) PROTOPY_OPNAME(POP_JUMP_IF_TRUE)
        PROTOPY_OPNAME(UNPACK_EX) PROTOPY_OPNAME(IMPORT_FROM) PROTOPY_OPNAME(PUSH_NULL)
        PROTOPY_OPNAME(BUILD_ANNOTATE) PROTOPY_OPNAME(WRAP_RAW_LIST) PROTOPY_OPNAME(BUILD_RAW_LIST)
        PROTOPY_OPNAME(DICT_MERGE) PROTOPY_OPNAME(ACC_FAST_FAST) PROTOPY_OPNAME(INC_FAST_K)
        PROTOPY_OPNAME(LT_FAST_FAST_JF) PROTOPY_OPNAME(STORE_ATTR_GROUP)
        PROTOPY_OPNAME(STORE_ATTR_GROUP_END) PROTOPY_OPNAME(DELETE_DEREF) PROTOPY_OPNAME(LOAD_CLOSURE)
        PROTOPY_OPNAME(LOAD_CLASSDEREF)
#undef PROTOPY_OPNAME
        default: return nullptr;
    }
}

namespace {

/** The code object's attribute `name`, or nullptr when it has none. */
const proto::ProtoObject* attr(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const char* name) {
    const proto::ProtoString* s = PythonEnvironment::getInternedString(ctx, name);
    if (obj->hasAttribute(ctx, s) != PROTO_TRUE) return nullptr;
    return obj->getAttribute(ctx, s);
}

long long intAttr(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const char* name, long long dflt = 0) {
    const proto::ProtoObject* v = attr(ctx, obj, name);
    return (v && v->isInteger(ctx)) ? v->asLong(ctx) : dflt;
}

std::string utf8(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    std::string s;
    if (v && v->isString(ctx)) v->asString(ctx)->toUTF8String(ctx, s);
    return s;
}

std::string strAttr(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const char* name) {
    return utf8(ctx, attr(ctx, obj, name));
}

/** The items of a tuple attribute (co_consts, co_names, ...). */
std::vector<const proto::ProtoObject*> items(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const char* name) {
    std::vector<const proto::ProtoObject*> out;
    const proto::ProtoObject* v = attr(ctx, obj, name);
    TupleView t = v ? PythonEnvironment::tupleView(ctx, v) : TupleView();
    if (!t) return out;
    for (proto::proto_ulong i = 0; i < t.size(ctx); ++i) out.push_back(t.at(ctx, i));
    return out;
}

bool isCodeObject(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (!v || v == PROTO_NONE || v->isInteger(ctx) || v->isDouble(ctx) || v->isBoolean(ctx) || v->isString(ctx))
        return false;
    return v->hasOwnAttribute(ctx, PythonEnvironment::getInternedString(ctx, "co_code")) == PROTO_TRUE;
}

std::string baseName(const std::string& path) {
    const std::string::size_type p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

std::string codeLabel(proto::ProtoContext* ctx, const proto::ProtoObject* code) {
    std::ostringstream s;
    s << "<code " << strAttr(ctx, code, "co_name") << ", line " << intAttr(ctx, code, "co_firstlineno") << ">";
    return s.str();
}

/** A constant as the listing shows it: kind and value. */
void describeConstant(proto::ProtoContext* ctx, const proto::ProtoObject* v, std::string& kind, std::string& text) {
    if (!v) { kind = "null"; text = "NULL"; return; }
    if (v == PROTO_NONE) { kind = "none"; text = "None"; return; }
    if (v == PROTO_TRUE || v == PROTO_FALSE) { kind = "bool"; text = v == PROTO_TRUE ? "True" : "False"; return; }
    if (isCodeObject(ctx, v)) { kind = "code"; text = codeLabel(ctx, v); return; }
    if (PythonEnvironment::isTupleValue(ctx, v)) {
        TupleView t = PythonEnvironment::tupleView(ctx, v);
        std::string s = "(";
        for (proto::proto_ulong i = 0; i < t.size(ctx); ++i) {
            std::string k, e;
            describeConstant(ctx, t.at(ctx, i), k, e);
            if (i) s += ", ";
            s += e;
        }
        if (t.size(ctx) == 1) s += ",";
        kind = "tuple";
        text = s + ")";
        return;
    }
    if (v->isInteger(ctx)) kind = "int";
    else if (v->isDouble(ctx)) kind = "float";
    else if (v->isString(ctx)) kind = "str";
    else {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        const proto::ProtoObject* type = env ? env->getType(ctx, v) : nullptr;
        kind = type ? strAttr(ctx, type, "__name__") : std::string();
        if (kind.empty()) kind = "object";
    }
    text = PythonEnvironment::reprObject(ctx, v);
}

const char* const kCompareOps[] = {"==", "!=", "<", "<=", ">", ">=", "in", "not in", "is", "is not",
                                   "exception match"};

class Lister {
public:
    Lister(proto::ProtoContext* ctx, std::ostream& out) : ctx_(ctx), out_(out) {}

    void list(const proto::ProtoObject* code, const std::string& path) {
        std::vector<const proto::ProtoObject*> nested;
        listOne(code, path, nested);
        for (size_t k = 0; k < nested.size(); ++k) {
            out_ << "\n";
            list(nested[k], path + "." + std::to_string(k));
        }
    }

private:
    proto::ProtoContext* ctx_;
    std::ostream& out_;
    std::vector<const proto::ProtoObject*> consts_, names_;
    std::map<long long, std::string> slotNames_;

    std::string constText(long long i) const {
        if (i < 0 || i >= static_cast<long long>(consts_.size())) return "?";
        std::string kind, text;
        describeConstant(ctx_, consts_[static_cast<size_t>(i)], kind, text);
        return text;
    }
    std::string nameText(long long i) const {
        if (i < 0 || i >= static_cast<long long>(names_.size())) return "?";
        return utf8(ctx_, names_[static_cast<size_t>(i)]);
    }
    std::string slotText(long long s) const {
        auto it = slotNames_.find(s);
        return it == slotNames_.end() ? "slot " + std::to_string(s) : it->second;
    }

    void listOne(const proto::ProtoObject* code, const std::string& path,
                 std::vector<const proto::ProtoObject*>& nested) {
        consts_ = items(ctx_, code, "co_consts");
        names_ = items(ctx_, code, "co_names");
        const auto varnames = items(ctx_, code, "co_varnames");
        const auto freevars = items(ctx_, code, "co_freevars");
        const auto cellvars = items(ctx_, code, "co_cellvars");
        const auto cellslots = items(ctx_, code, "co_cellslots");

        slotNames_.clear();
        for (size_t i = 0; i < varnames.size(); ++i) slotNames_[static_cast<long long>(i)] = utf8(ctx_, varnames[i]);
        for (size_t j = 0; j < freevars.size(); ++j)
            slotNames_[static_cast<long long>(varnames.size() + j)] = "free " + utf8(ctx_, freevars[j]);
        for (size_t k = 0; k < cellslots.size() && k < cellvars.size(); ++k) {
            if (!cellslots[k]->isInteger(ctx_)) continue;
            slotNames_[cellslots[k]->asLong(ctx_)] = "cell " + utf8(ctx_, cellvars[k]);
        }

        const long long flags = intAttr(ctx_, code, "co_flags");
        out_ << "code " << path << ": " << strAttr(ctx_, code, "co_name") << " ("
             << baseName(strAttr(ctx_, code, "co_filename")) << ", line " << intAttr(ctx_, code, "co_firstlineno")
             << ")\n";
        out_ << "  flags 0x" << std::hex << flags << std::dec;
        static const std::pair<long long, const char*> kFlags[] = {
            {CO_OPTIMIZED, "OPTIMIZED"}, {CO_NEWLOCALS, "NEWLOCALS"}, {CO_VARARGS, "VARARGS"},
            {CO_VARKEYWORDS, "VARKEYWORDS"}, {CO_NESTED, "NESTED"}, {0x20, "GENERATOR"}, {0x80, "COROUTINE"},
            {0x100, "ITERABLE_COROUTINE"}, {0x200, "ASYNC_GENERATOR"}};
        std::string names;
        for (const auto& f : kFlags)
            if (flags & f.first) names += (names.empty() ? "" : " | ") + std::string(f.second);
        if (!names.empty()) out_ << " (" << names << ")";
        out_ << ", parameters " << intAttr(ctx_, code, "co_nparams")
             << " (" << intAttr(ctx_, code, "co_posonlyargcount") << " positional-only, "
             << intAttr(ctx_, code, "co_kwonlyargcount") << " keyword-only)";
        const proto::ProtoObject* isGen = attr(ctx_, code, "co_is_generator");
        if (isGen == PROTO_TRUE) out_ << ", generator";
        out_ << "\n";
        const long long nlp = intAttr(ctx_, code, "co_nlocalsplus", static_cast<long long>(varnames.size()));
        out_ << "  slots " << intAttr(ctx_, code, "co_automatic_count") << ": ";
        if (nlp > 0) out_ << "variables 0.." << nlp - 1 << ", ";
        out_ << "operand stack from " << nlp << "\n";

        auto listNames = [&](const char* title, const std::vector<const proto::ProtoObject*>& v, long long base) {
            if (v.empty()) return;
            out_ << "  " << title << ":\n";
            for (size_t i = 0; i < v.size(); ++i)
                out_ << "    " << std::left << std::setw(5) << ("#" + std::to_string(base + static_cast<long long>(i)))
                     << std::right << utf8(ctx_, v[i]) << "\n";
        };

        if (!consts_.empty()) {
            out_ << "  constants:\n";
            for (size_t i = 0; i < consts_.size(); ++i) {
                std::string kind, text;
                describeConstant(ctx_, consts_[i], kind, text);
                if (kind == "code") {
                    text += "  -> code " + path + "." + std::to_string(nested.size());
                    nested.push_back(consts_[i]);
                }
                out_ << "    " << std::left << std::setw(5) << ("#" + std::to_string(i)) << std::setw(7) << kind
                     << std::right << " " << text << "\n";
            }
        }
        listNames("names", names_, 0);
        listNames("variables (slot)", varnames, 0);
        listNames("free variables (slot)", freevars, static_cast<long long>(varnames.size()));
        if (!cellvars.empty()) {
            out_ << "  cells:\n";
            for (size_t k = 0; k < cellvars.size(); ++k) {
                out_ << "    " << utf8(ctx_, cellvars[k]);
                if (k < cellslots.size() && cellslots[k]->isInteger(ctx_)) out_ << " in slot " << cellslots[k]->asLong(ctx_);
                out_ << "\n";
            }
        }
        const proto::ProtoObject* selfRef = attr(ctx_, code, "co_selfref");
        if (selfRef && selfRef->isInteger(ctx_)) {
            const long long j = selfRef->asLong(ctx_);
            out_ << "  self-reference: free variable #" << static_cast<long long>(varnames.size()) + j
                 << (attr(ctx_, code, "co_selfref_cell") == PROTO_TRUE ? " (in a cell)" : "") << "\n";
        }

        // Instructions.  The line table (co_lnotab) holds (instruction delta,
        // signed line delta) byte pairs from co_firstlineno.
        const auto bc = items(ctx_, code, "co_code");
        std::vector<int> words;
        for (const auto* w : bc) words.push_back(w && w->isInteger(ctx_) ? static_cast<int>(w->asLong(ctx_)) : 0);
        std::map<long long, long long> lineAt;  // instruction index -> line
        {
            const auto tab = items(ctx_, code, "co_lnotab");
            long long line = intAttr(ctx_, code, "co_firstlineno");
            long long pc = 0;
            lineAt[0] = line;
            for (size_t k = 0; k + 1 < tab.size(); k += 2) {
                pc += tab[k]->asLong(ctx_);
                line += static_cast<signed char>(static_cast<unsigned char>(tab[k + 1]->asLong(ctx_)));
                lineAt[pc] = line;
            }
        }
        out_ << "  code:\n";
        long long lastLine = -1;
        for (size_t i = 0; i < words.size(); i += 2) {
            const int op = words[i];
            const int arg = i + 1 < words.size() ? words[i + 1] : 0;
            long long line = -1;
            auto it = lineAt.upper_bound(static_cast<long long>(i / 2));
            if (it != lineAt.begin()) line = std::prev(it)->second;
            std::ostringstream lineCol;
            if (line != lastLine) lineCol << line;
            lastLine = line;
            const char* name = opcodeName(op);
            std::ostringstream row;
            row << "  " << std::setw(4) << lineCol.str() << std::setw(6) << i << "  " << std::left << std::setw(24)
                << (name ? name : ("<" + std::to_string(op) + ">")) << std::right << std::setw(7) << arg;
            const std::string note = annotate(op, arg, i, words);
            std::string text = row.str();
            if (!note.empty()) text += "  ; " + note;
            while (!text.empty() && text.back() == ' ') text.pop_back();
            out_ << text << "\n";
        }
    }

    std::string annotate(int op, int arg, size_t i, const std::vector<int>& words) const {
        auto jumpTo = [](long long target) { return "to " + std::to_string(target); };
        switch (op) {
            case OP_LOAD_CONST:
                return constText(arg);
            case OP_LOAD_NAME: case OP_LOAD_GLOBAL:
                return nameText(arg >> 1) + ((arg & 1) ? " (+ NULL)" : "");
            case OP_LOAD_ATTR:
                return nameText(arg >> 1) + ((arg & 1) ? " (method)" : "");
            case OP_STORE_NAME: case OP_DELETE_NAME: case OP_STORE_GLOBAL: case OP_DELETE_GLOBAL:
            case OP_STORE_ATTR: case OP_DELETE_ATTR: case OP_IMPORT_FROM:
                return nameText(arg >> 1);
            case OP_LOAD_FAST: case OP_STORE_FAST: case OP_DELETE_FAST:
                return slotText(arg);
            case OP_LOAD_DEREF: case OP_STORE_DEREF: case OP_DELETE_DEREF: case OP_LOAD_CLASSDEREF:
                return slotText(arg);
            case OP_LOAD_CLOSURE:
                return arg < 0 ? std::string("a new empty cell") : slotText(arg);
            case OP_COMPARE_OP:
                return (arg >= 0 && arg <= 10) ? kCompareOps[arg] : "?";
            case OP_POP_JUMP_IF_FALSE: case OP_POP_JUMP_IF_TRUE: case OP_JUMP_ABSOLUTE: case OP_FOR_ITER:
            case OP_SETUP_FINALLY: case OP_SETUP_WITH: case OP_SETUP_ASYNC_WITH:
                return jumpTo(arg);
            case OP_JUMP_FORWARD:
                return jumpTo(static_cast<long long>(i) + 2 + arg);
            case OP_CALL_FUNCTION:
                return std::to_string(arg) + " positional";
            case OP_CALL_FUNCTION_KW:
                return std::to_string(arg) + " arguments, the last ones named by the tuple on top";
            case OP_CALL_FUNCTION_EX:
                return (arg & 1) ? "positional sequence and keyword mapping" : "positional sequence";
            case OP_BUILD_FUNCTION: {
                std::string s;
                auto add = [&](const char* part) { s += (s.empty() ? "" : ", ") + std::string(part); };
                if (arg & 0x01) add("defaults");
                if (arg & 0x02) add("keyword defaults");
                if (arg & 0x04) add("annotations");
                const int cells = arg >> BUILD_FUNCTION_CELLS_SHIFT;
                if (cells) add((std::to_string(cells) + (cells == 1 ? " closure cell" : " closure cells")).c_str());
                return s;
            }
            case OP_RAISE_VARARGS:
                return arg == 0 ? "re-raise" : arg == 1 ? "raise TOS" : arg == 2 ? "raise TOS1 from TOS" : "?";
            case OP_UNPACK_EX:
                return std::to_string(arg & 0xFF) + " before, " + std::to_string((arg >> 8) & 0xFF) + " after";
            case OP_DICT_MERGE:
                return "dict at depth " + std::to_string(arg & ~DICT_MERGE_CALL_SITE) +
                       ((arg & DICT_MERGE_CALL_SITE) ? ", call site" : "");
            case OP_ACC_FAST_FAST:
                return slotText((arg >> 8) & 0xFF) + " += " + slotText(arg & 0xFF);
            case OP_INC_FAST_K:
                return slotText((arg >> 8) & 0xFF) + " += " + constText(arg & 0xFF);
            case OP_LT_FAST_FAST_JF: {
                const size_t t = i + 7;
                const long long target = t < words.size() ? words[t] : -1;
                return "if not " + slotText((arg >> 8) & 0xFF) + " < " + slotText(arg & 0xFF) + ": " + jumpTo(target);
            }
            case OP_STORE_ATTR_GROUP: case OP_STORE_ATTR_GROUP_END: {
                const long long d = arg >> 5;
                const int pos = arg & 31;
                std::string name = "?";
                if (d >= 0 && d < static_cast<long long>(consts_.size())) {
                    TupleView t = PythonEnvironment::tupleView(ctx_, consts_[static_cast<size_t>(d)]);
                    if (t && static_cast<proto::proto_ulong>(1 + pos) < t.size(ctx_))
                        name = nameText(t.at(ctx_, static_cast<proto::proto_ulong>(1 + pos))->asLong(ctx_));
                }
                return name + " (write " + std::to_string(pos) + ", descriptor #" + std::to_string(d) + ")";
            }
            default:
                return std::string();
        }
    }
};

} // namespace

void disassemble(proto::ProtoContext* ctx, const proto::ProtoObject* code, std::ostream& out) {
    if (!ctx || !isCodeObject(ctx, code)) return;
    Lister(ctx, out).list(code, "0");
}

int disassembleFile(proto::ProtoContext* ctx, const std::string& path, std::ostream& out, std::ostream& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err << "protopy: cannot read " << path << "\n";
        return 1;
    }
    std::stringstream buf;
    buf << f.rdbuf();
    Parser parser(buf.str());
    std::unique_ptr<ModuleNode> mod = parser.parseModule();
    if (parser.hasError() || !mod) {
        err << baseName(path) << ":" << parser.getLastErrorLine() << ": SyntaxError: " << parser.getLastErrorMsg()
            << "\n";
        return 1;
    }
    try {
        // As PythonEnvironment::executeModule compiles a module.
        Compiler compiler(ctx, path);
        if (!compiler.compileModule(mod.get())) {
            err << baseName(path) << ": the module does not compile\n";
            return 1;
        }
        const proto::ProtoObject* code = makeCodeObject(ctx, compiler.getConstants(), compiler.getNames(),
            compiler.getBytecode(), PythonEnvironment::getInternedString(ctx, path.c_str()), nullptr, 0, 0,
            compiler.getMaxStack() + 32, 0, false, PythonEnvironment::getInternedString(ctx, "<module>"),
            compiler.getFirstLine(), compiler.getLnotab());
        // The code object is held only here while the listing allocates
        // (constant reprs): pinned, as protoCore's embedder rule 3 asks.
        PythonEnvironment::TransientPin pin(PythonEnvironment::fromContext(ctx), code);
        disassemble(ctx, code, out);
    } catch (const proto::ProtoException& e) {
        err << baseName(path) << ": " << PythonEnvironment::reprObject(ctx, e.value()) << "\n";
        return 1;
    }
    return 0;
}

} // namespace protoPython
