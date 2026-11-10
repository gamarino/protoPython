#ifndef PROTOPYTHON_BUILTINSIGNATURE_H
#define PROTOPYTHON_BUILTINSIGNATURE_H

#include <protoCore.h>
#include <vector>

namespace protoPython {

// Keyword arguments of the builtin functions, checked against CPython 3.14's
// signatures.  A builtin's native method reads its parameters by position;
// a builtin registered through withSignature<> receives the parameters that
// may come by keyword (round(x, ndigits=2), pow(2, 5, mod=3)) moved to their
// positions, and an unknown keyword raises TypeError with CPython's message
// instead of being ignored.  Keyword-only parameters the method reads itself
// (sorted's key=, print's sep=) are listed so they pass through unchanged.
struct BuiltinParam {
    const char* name;
    enum Default { Required, None, Int, Bool, Str } kind;
    long long value;
    const char* text = nullptr;   // the default of a Str parameter
};
struct BuiltinSignature {
    const char* fn;                  // the name in error messages
    unsigned positionalOnly;         // leading parameters that never come by keyword
    std::vector<BuiltinParam> positional;   // positional-or-keyword, after them
    std::vector<const char*> keywordOnly;   // read by the method from the keywords
    // Unknown keywords pass through instead of raising: for a __new__ that,
    // as in CPython, leaves the other arguments to a subclass's __init__.
    bool passUnknown = false;
};

// Calls `impl` with the keywords that name positional parameters moved to
// their positions; nullptr after raising TypeError for a keyword the
// signature does not have, a parameter given twice, or a required one
// missing before a later one given by keyword.  Without keywords it calls
// `impl` directly.
const proto::ProtoObject* callWithSignature(
    proto::ProtoMethod impl, const BuiltinSignature& sig,
    proto::ProtoContext* context, const proto::ProtoObject* self, const proto::ParentLink* parentLink,
    const proto::ProtoArguments* args, const proto::ProtoKeywordArguments* kwargs);

// A ProtoMethod that checks the keywords of `Impl` against `Sig`.
template <proto::ProtoMethod Impl, const BuiltinSignature& Sig>
const proto::ProtoObject* withSignature(
    proto::ProtoContext* context, const proto::ProtoObject* self, const proto::ParentLink* parentLink,
    const proto::ProtoArguments* args, const proto::ProtoKeywordArguments* kwargs) {
    return callWithSignature(Impl, Sig, context, self, parentLink, args, kwargs);
}

} // namespace protoPython

#endif
