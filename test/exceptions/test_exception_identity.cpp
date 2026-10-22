// test_exception_identity.cpp -- protoCore's rule E3 through Python code.
//
// An exception no Python handler selects must leave unchanged: the very C++
// exception object that entered, never a new ProtoException with the same
// value (protoCore docs/EXCEPTIONS.md, rule E3).  Another runtime may throw a
// subclass of proto::ProtoException with extras of its own (a traceback, a
// source position); a runtime that re-raises by value would slice them off.
//
// `raiser` is a native method that throws ForeignException, such a subclass
// with an `extra` field.  Each function of exception_identity_probe.py runs
// it through one path (an except chain that matches nothing, a finally
// clause, a bare raise, a with statement, a generator, yield from, nested
// calls); what reaches C++ must be a ForeignException (dynamic_cast), with
// its extra intact and the same value.  `raise e` of the caught value is a
// new raise in Python: it keeps the value, and is checked for that alone.
//
//     test_exception_identity [-p <dir>]
//
// -p puts <dir> first on the module path: the protopyc-compiled probe
// (protopyc_exception_identity, test/compiler) is found there instead of the
// source.  Prints one line per check and "exception_identity: PASS".

#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct ForeignException : proto::ProtoException {
    ForeignException(proto::ProtoContext* ctx, const proto::ProtoObject* value, int extra)
        : proto::ProtoException(ctx, value), extra(extra) {}
    int extra;
};

constexpr int kExtra = 0x5EED;
const proto::ProtoObject* g_value = nullptr;

const proto::ProtoObject* raiser(proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
                                 const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) {
    throw ForeignException(ctx, g_value, kExtra);
}

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> paths;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-p") == 0 && i + 1 < argc) paths.push_back(argv[++i]);
    }
    if (paths.empty()) paths.push_back(PROBE_DIR);

    protoPython::PythonEnvironment env(STDLIB_PATH, paths, {"test_exception_identity"});
    proto::ProtoContext* ctx = env.getContext();
    // Another language's error value: a plain protoCore object, kept alive
    // by the module namespace below.
    g_value = ctx->newObject(false)->setAttribute(
        ctx, proto::ProtoString::createSymbol(ctx, "message"), ctx->fromUTF8String("foreign"));

    env.executeString("import exception_identity_probe as _probe\n", "<identity>");
    const proto::ProtoObject* main = env.resolve("__main__", ctx);
    env.setAttr(main, "_raiser", ctx->fromMethod(nullptr, &raiser));
    env.setAttr(main, "_foreign_value", g_value);
    const proto::ProtoObject* probe = env.getAttr(main, "_probe");
    const proto::ProtoObject* file = probe ? env.getAttr(probe, "__file__") : nullptr;
    std::string where = "?";
    if (file && file->isString(ctx)) file->asString(ctx)->toUTF8String(ctx, where);
    std::printf("probe: %s\n", where.c_str());

    const char* cases[] = {"unmatched_except", "nested_unmatched", "through_finally", "bare_raise",
                           "bare_raise_after_nested", "through_with", "through_generator",
                           "through_yield_from", "through_calls"};
    for (const char* name : cases) {
        bool escaped = false, sameObjectType = false, extraKept = false, sameValue = false;
        try {
            env.executeString(std::string("_probe.") + name + "(_raiser)\n", "<identity>");
        } catch (const proto::ProtoException& e) {
            escaped = true;
            const auto* foreign = dynamic_cast<const ForeignException*>(&e);
            sameObjectType = foreign != nullptr;
            extraKept = foreign && foreign->extra == kExtra;
            sameValue = e.value() == g_value;
        } catch (...) {
        }
        check(escaped && sameObjectType && extraKept && sameValue,
              std::string(name) + ": escaped=" + (escaped ? "1" : "0") + " subclass=" +
                  (sameObjectType ? "1" : "0") + " extra=" + (extraKept ? "1" : "0") +
                  " value=" + (sameValue ? "1" : "0"));
    }

    // `raise e` is a new raise of the same object (Python semantics): the
    // value is the same; the C++ object is protoPython's own.
    bool valueKept = false;
    try {
        env.executeString(
            "try:\n"
            "    _raiser()\n"
            "except BaseException as e:\n"
            "    raise e\n", "<identity>");
    } catch (const proto::ProtoException& e) {
        valueKept = e.value() == g_value;
    }
    check(valueKept, "raise e keeps the value");

    std::printf("exception_identity: %s\n", g_failures ? "FAILED" : "PASS");
    return g_failures ? 1 : 0;
}
