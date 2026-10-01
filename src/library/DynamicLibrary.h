// DynamicLibrary.h — dlopen and friends on every platform.
//
// POSIX has <dlfcn.h>. Windows loads a DLL with LoadLibraryExW instead, so there
// this header supplies the names the module providers use, with the same
// contract: dlopen returns nullptr on failure and dlerror then describes it.
// RTLD_GLOBAL and RTLD_LOCAL mean nothing on Windows, where every DLL keeps its
// own symbol namespace and a module imports what it needs by name.
//
// PROTOPY_SHLIB_SUFFIX is the file suffix of a loadable module: ".so" on Linux
// and macOS (as before), ".dll" on Windows.
#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

#define RTLD_LAZY   0x1
#define RTLD_NOW    0x2
#define RTLD_LOCAL  0x0
#define RTLD_GLOBAL 0x100

#define PROTOPY_SHLIB_SUFFIX ".dll"

namespace protoPython::detail {
inline thread_local std::string tl_dlerror;

inline void dlSetError(const std::string& what) {
    const DWORD code = ::GetLastError();
    char* text = nullptr;
    ::FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                         FORMAT_MESSAGE_IGNORE_INSERTS,
                     nullptr, code, 0, reinterpret_cast<char*>(&text), 0, nullptr);
    tl_dlerror = what + ": " + (text ? text : ("error " + std::to_string(code)));
    if (text) ::LocalFree(text);
    while (!tl_dlerror.empty() && (tl_dlerror.back() == '\n' || tl_dlerror.back() == '\r'))
        tl_dlerror.pop_back();
}
} // namespace protoPython::detail

inline void* dlopen(const char* path, int /*flags*/) {
    // Paths are UTF-8 in protoPython and UTF-16 for the system. The DLL's own
    // directory is searched for its dependencies, which needs an absolute path.
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    std::wstring wide(n > 0 ? static_cast<std::size_t>(n) : 1u, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, path, -1, wide.data(), n);
    std::wstring full(32768, L'\0');
    const DWORD len = ::GetFullPathNameW(wide.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
    HMODULE module = ::LoadLibraryExW(len > 0 && len < full.size() ? full.c_str() : wide.c_str(), nullptr,
                                      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) protoPython::detail::dlSetError(path);
    return module;
}

inline void* dlsym(void* handle, const char* name) {
    FARPROC proc = ::GetProcAddress(static_cast<HMODULE>(handle), name);
    if (!proc) protoPython::detail::dlSetError(name);
    return reinterpret_cast<void*>(proc);
}

inline int dlclose(void* handle) {
    return ::FreeLibrary(static_cast<HMODULE>(handle)) ? 0 : -1;
}

inline const char* dlerror() {
    return protoPython::detail::tl_dlerror.c_str();
}

#else
#include <dlfcn.h>
#define PROTOPY_SHLIB_SUFFIX ".so"
#endif
