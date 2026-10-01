// WinapiModule.cpp — the part of CPython's _winapi that the standard library
// needs at import time on Windows. Built on Windows only.
//
// shutil imports _winapi unconditionally on win32 and calls
// NeedCurrentDirectoryForExePath from shutil.which(); ntpath.normcase uses
// LCMapStringEx when it is there. Process creation (CreateProcess, pipes,
// handles) is not provided, so subprocess, which imports those names, raises
// ImportError on Windows, as does asyncio's Windows event loop (_overlapped).
#if defined(_WIN32)

#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "PosixCompat.h"

namespace protoPython {
namespace winapi_module {

static std::string utf8Arg(proto::ProtoContext* ctx, const proto::ProtoList* args, int i) {
    std::string s;
    if (args && args->getSize(ctx) > static_cast<proto::proto_ulong>(i)) {
        const proto::ProtoObject* o = args->getAt(ctx, i);
        if (o && o->isString(ctx)) o->asString(ctx)->toUTF8String(ctx, s);
    }
    return s;
}

// NeedCurrentDirectoryForExePath(exe_name) -> bool
static const proto::ProtoObject* py_need_current_directory_for_exe_path(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    std::wstring name = protopy_widen(utf8Arg(ctx, args, 0));
    return NeedCurrentDirectoryForExePathW(name.c_str()) ? PROTO_TRUE : PROTO_FALSE;
}

// LCMapStringEx(locale, flags, src) -> str
static const proto::ProtoObject* py_lc_map_string_ex(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    std::wstring locale = protopy_widen(utf8Arg(ctx, args, 0));
    DWORD flags = 0;
    if (args && args->getSize(ctx) > 1 && args->getAt(ctx, 1)->isInteger(ctx))
        flags = static_cast<DWORD>(args->getAt(ctx, 1)->asLong(ctx));
    std::wstring src = protopy_widen(utf8Arg(ctx, args, 2));
    if (src.empty()) return PythonEnvironment::getInternedString(ctx, "")->asObject(ctx);
    const wchar_t* loc = locale.empty() ? LOCALE_NAME_INVARIANT : locale.c_str();
    int n = LCMapStringEx(loc, flags, src.c_str(), static_cast<int>(src.size()),
                          nullptr, 0, nullptr, nullptr, 0);
    if (n <= 0) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        int err = protopy_errno_from_win32(GetLastError());
        if (env) env->raiseOSError(ctx, err, "LCMapStringEx failed", "");
        return nullptr;
    }
    std::wstring out(static_cast<size_t>(n), L'\0');
    LCMapStringEx(loc, flags, src.c_str(), static_cast<int>(src.size()),
                  out.data(), n, nullptr, nullptr, 0);
    return proto::ProtoString::fromUTF8(ctx, protopy_narrow(out).c_str())->asObject(ctx);
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(false);
    auto fn = [&](const char* name, proto::ProtoMethod m) {
        mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, name),
            ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), m));
    };
    auto num = [&](const char* name, long long v) {
        mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, name), ctx->fromInteger(v));
    };
    fn("NeedCurrentDirectoryForExePath", py_need_current_directory_for_exe_path);
    fn("LCMapStringEx", py_lc_map_string_ex);
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "LOCALE_NAME_INVARIANT"),
        PythonEnvironment::getInternedString(ctx, "")->asObject(ctx));
    num("LCMAP_LOWERCASE", LCMAP_LOWERCASE);
    num("LCMAP_UPPERCASE", LCMAP_UPPERCASE);
    num("ERROR_FILE_NOT_FOUND", ERROR_FILE_NOT_FOUND);
    num("ERROR_PATH_NOT_FOUND", ERROR_PATH_NOT_FOUND);
    num("ERROR_ACCESS_DENIED", ERROR_ACCESS_DENIED);
    num("ERROR_ALREADY_EXISTS", ERROR_ALREADY_EXISTS);
    num("ERROR_PRIVILEGE_NOT_HELD", ERROR_PRIVILEGE_NOT_HELD);
    num("ERROR_SHARING_VIOLATION", ERROR_SHARING_VIOLATION);
    num("INFINITE", static_cast<long long>(INFINITE));
    return mod;
}

} // namespace winapi_module
} // namespace protoPython

#endif // _WIN32
