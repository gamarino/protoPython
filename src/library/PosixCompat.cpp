// PosixCompat.cpp — Windows implementation of PosixCompat.h. Built on Windows
// only (src/library/CMakeLists.txt); see the header for what it provides.
#if defined(_WIN32)

#include "PosixCompat.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#include <tlhelp32.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

// The macros of PosixCompat.h rename these; the definitions below need the
// real names.
#undef stat
#undef lstat
#undef fstat
#undef open
#undef mkdir
#undef rmdir
#undef unlink
#undef chdir
#undef getcwd
#undef access
#undef readlink
#undef fcntl
#undef utimensat
#undef setenv
#undef unsetenv
#undef pipe
#undef getppid

std::wstring protopy_widen(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

std::string protopy_narrow(const std::wstring& utf16) {
    if (utf16.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()),
                        out.data(), n, nullptr, nullptr);
    return out;
}

std::string protopy_executable_path() {
    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return std::string();
        if (n < buf.size()) return protopy_narrow(std::wstring(buf.data(), n));
        buf.resize(buf.size() * 2);
    }
}

static void ignoreInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {}

protopy_iph protopy_ignore_invalid_parameters() {
    return _set_thread_local_invalid_parameter_handler(ignoreInvalidParameter);
}

void protopy_restore_invalid_parameters(protopy_iph previous) {
    _set_thread_local_invalid_parameter_handler(previous);
}

void protopy_ignore_invalid_parameters_process_wide() {
    _set_invalid_parameter_handler(ignoreInvalidParameter);
}

unsigned long protopy_current_thread_id() { return GetCurrentThreadId(); }

// CPython's winerror_to_errno (PC/errmap.h), for the codes file functions return.
int protopy_errno_from_win32(unsigned long winerror) {
    switch (winerror) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
    case ERROR_NO_MORE_FILES:
    case ERROR_BAD_NETPATH:
    case ERROR_BAD_NET_NAME:
    case ERROR_BAD_PATHNAME:
    case ERROR_FILENAME_EXCED_RANGE:
    case ERROR_INVALID_NAME:
    case ERROR_DIRECTORY:
        return winerror == ERROR_DIRECTORY ? ENOTDIR : ENOENT;
    case ERROR_ACCESS_DENIED:
    case ERROR_CURRENT_DIRECTORY:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_NETWORK_ACCESS_DENIED:
    case ERROR_CANNOT_MAKE:
    case ERROR_FAIL_I24:
    case ERROR_DRIVE_LOCKED:
    case ERROR_SEEK_ON_DEVICE:
    case ERROR_NOT_LOCKED:
    case ERROR_LOCK_FAILED:
    case ERROR_PRIVILEGE_NOT_HELD:
        return EACCES;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
        return EEXIST;
    case ERROR_DIR_NOT_EMPTY:
        return ENOTEMPTY;
    case ERROR_NOT_SAME_DEVICE:
        return EXDEV;
    case ERROR_INVALID_HANDLE:
    case ERROR_INVALID_TARGET_HANDLE:
    case ERROR_DIRECT_ACCESS_HANDLE:
        return EBADF;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        return ENOMEM;
    case ERROR_DISK_FULL:
        return ENOSPC;
    case ERROR_BROKEN_PIPE:
    case ERROR_NO_DATA:
        return EPIPE;
    case ERROR_TOO_MANY_OPEN_FILES:
        return EMFILE;
    case ERROR_NOT_A_REPARSE_POINT:
    case ERROR_INVALID_PARAMETER:
    case ERROR_NEGATIVE_SEEK:
    default:
        return EINVAL;
    }
}

static int fail_win32() {
    errno = protopy_errno_from_win32(GetLastError());
    return -1;
}

// --- stat ------------------------------------------------------------------------

// 100 ns ticks since 1601-01-01 → seconds and nanoseconds since 1970-01-01.
static struct timespec filetime_to_timespec(const FILETIME& ft) {
    ULARGE_INTEGER t;
    t.LowPart = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    const long long ticks = static_cast<long long>(t.QuadPart) - 116444736000000000LL;
    long long sec = ticks / 10000000LL;
    long long rem = ticks % 10000000LL;
    if (rem < 0) { rem += 10000000LL; sec -= 1; }
    struct timespec ts;
    ts.tv_sec = static_cast<time_t>(sec);
    ts.tv_nsec = static_cast<long>(rem * 100);
    return ts;
}

static FILETIME timespec_to_filetime(const struct timespec& ts) {
    const long long ticks = static_cast<long long>(ts.tv_sec) * 10000000LL
                          + ts.tv_nsec / 100 + 116444736000000000LL;
    FILETIME ft;
    ft.dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFFLL);
    ft.dwHighDateTime = static_cast<DWORD>(static_cast<unsigned long long>(ticks) >> 32);
    return ft;
}

static bool has_exec_extension(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    size_t sep = path.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (sep != std::wstring::npos && dot < sep)) return false;
    std::wstring ext = path.substr(dot);
    for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
    return ext == L".exe" || ext == L".bat" || ext == L".cmd" || ext == L".com";
}

// CPython's attributes_to_mode plus the executable bits _wstat adds.
static unsigned int attributes_to_mode(DWORD attr, const std::wstring& path) {
    unsigned int mode = 0;
    if (attr & FILE_ATTRIBUTE_DIRECTORY) mode |= S_IFDIR | 0111;
    else mode |= S_IFREG;
    if (attr & FILE_ATTRIBUTE_READONLY) mode |= 0444;
    else mode |= 0666;
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY) && has_exec_extension(path)) mode |= 0111;
    return mode;
}

static void fill_from_info(struct protopy_stat* st, const BY_HANDLE_FILE_INFORMATION& info,
                           const std::wstring& path, bool isLink, unsigned long reparseTag = 0) {
    std::memset(st, 0, sizeof(*st));
    st->st_mode = isLink ? (S_IFLNK | 0777) : attributes_to_mode(info.dwFileAttributes, path);
    st->st_file_attributes = info.dwFileAttributes;
    st->st_reparse_tag = reparseTag;
    st->st_ino = (static_cast<unsigned long long>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    st->st_dev = info.dwVolumeSerialNumber;
    st->st_nlink = info.nNumberOfLinks;
    st->st_size = static_cast<long long>((static_cast<unsigned long long>(info.nFileSizeHigh) << 32)
                                         | info.nFileSizeLow);
    st->st_atim = filetime_to_timespec(info.ftLastAccessTime);
    st->st_mtim = filetime_to_timespec(info.ftLastWriteTime);
    st->st_ctim = filetime_to_timespec(info.ftCreationTime);
}

// The reparse tag of a path that is a name-surrogate reparse point (a
// symbolic link or a junction, which lstat() does not follow), else 0.
static unsigned long link_reparse_tag(const std::wstring& wpath) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wpath.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    FindClose(h);
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return 0;
    return IsReparseTagNameSurrogate(fd.dwReserved0) ? fd.dwReserved0 : 0;
}

static int stat_impl(const char* path, struct protopy_stat* st, bool followLinks) {
    std::wstring wpath = protopy_widen(path);
    bool isLink = false;
    unsigned long reparseTag = 0;
    DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;
    if (!followLinks) {
        // lstat(): a symbolic link is S_IFLNK; a junction is reported as the
        // directory it is, with its reparse tag (CPython's rules).
        reparseTag = link_reparse_tag(wpath);
        if (reparseTag != 0) {
            isLink = reparseTag == IO_REPARSE_TAG_SYMLINK;
            flags |= FILE_FLAG_OPEN_REPARSE_POINT;
        }
    }
    HANDLE h = CreateFileW(wpath.c_str(), FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, flags, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        // A file another process holds without FILE_SHARE_*: its directory
        // entry still answers, as in CPython's fallback.
        if (err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED) {
            WIN32_FIND_DATAW fd;
            HANDLE fh = FindFirstFileW(wpath.c_str(), &fd);
            if (fh != INVALID_HANDLE_VALUE) {
                FindClose(fh);
                BY_HANDLE_FILE_INFORMATION info = {};
                info.dwFileAttributes = fd.dwFileAttributes;
                info.ftCreationTime = fd.ftCreationTime;
                info.ftLastAccessTime = fd.ftLastAccessTime;
                info.ftLastWriteTime = fd.ftLastWriteTime;
                info.nFileSizeHigh = fd.nFileSizeHigh;
                info.nFileSizeLow = fd.nFileSizeLow;
                info.nNumberOfLinks = 1;
                fill_from_info(st, info, wpath, isLink, reparseTag);
                return 0;
            }
        }
        errno = protopy_errno_from_win32(err);
        return -1;
    }
    BY_HANDLE_FILE_INFORMATION info;
    BOOL ok = GetFileInformationByHandle(h, &info);
    DWORD err = GetLastError();
    CloseHandle(h);
    if (!ok) {
        errno = protopy_errno_from_win32(err);
        return -1;
    }
    fill_from_info(st, info, wpath, isLink, reparseTag);
    return 0;
}

int protopy_stat(const char* path, struct protopy_stat* st) { return stat_impl(path, st, true); }
int protopy_lstat(const char* path, struct protopy_stat* st) { return stat_impl(path, st, false); }

int protopy_fstat(int fd, struct protopy_stat* st) {
    HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    if (h == INVALID_HANDLE_VALUE) { errno = EBADF; return -1; }
    std::memset(st, 0, sizeof(*st));
    DWORD type = GetFileType(h);
    if (type == FILE_TYPE_DISK) {
        BY_HANDLE_FILE_INFORMATION info;
        if (!GetFileInformationByHandle(h, &info)) return fail_win32();
        fill_from_info(st, info, std::wstring(), false);
        return 0;
    }
    // Consoles are character devices and pipes FIFOs, as CPython reports them.
    st->st_mode = (type == FILE_TYPE_CHAR) ? (S_IFCHR | 0666)
                : (type == FILE_TYPE_PIPE) ? (S_IFIFO | 0666) : 0;
    if (type == FILE_TYPE_PIPE) {
        DWORD avail = 0;
        if (PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) st->st_size = avail;
    }
    return 0;
}

// --- Directories -------------------------------------------------------------------

struct DIR {
    HANDLE handle;
    WIN32_FIND_DATAW data;
    bool first;
    struct dirent entry;
};

DIR* opendir(const char* path) {
    std::wstring pattern = protopy_widen(path);
    if (pattern.empty()) { errno = ENOENT; return nullptr; }
    DWORD attr = GetFileAttributesW(pattern.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) { fail_win32(); return nullptr; }
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) { errno = ENOTDIR; return nullptr; }
    wchar_t last = pattern.back();
    if (last != L'\\' && last != L'/' && last != L':') pattern += L'\\';
    pattern += L'*';
    DIR* d = new DIR();
    d->handle = FindFirstFileW(pattern.c_str(), &d->data);
    if (d->handle == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        delete d;
        errno = protopy_errno_from_win32(err);
        return nullptr;
    }
    d->first = true;
    return d;
}

struct dirent* readdir(DIR* d) {
    if (!d || d->handle == INVALID_HANDLE_VALUE) return nullptr;
    if (!d->first) {
        if (!FindNextFileW(d->handle, &d->data)) return nullptr;
    }
    d->first = false;
    std::string name = protopy_narrow(d->data.cFileName);
    std::strncpy(d->entry.d_name, name.c_str(), sizeof(d->entry.d_name) - 1);
    d->entry.d_name[sizeof(d->entry.d_name) - 1] = '\0';
    return &d->entry;
}

int closedir(DIR* d) {
    if (!d) return -1;
    if (d->handle != INVALID_HANDLE_VALUE) FindClose(d->handle);
    delete d;
    return 0;
}

// --- Paths and files ----------------------------------------------------------------

// open() through CreateFileW so that the file can be renamed or removed while
// it is open (FILE_SHARE_DELETE), as on POSIX: protoPython closes a file
// object when it is closed or collected, not when its last reference goes,
// and _wopen's sharing mode made os.replace/os.remove of such a file fail
// with EACCES. The C runtime's flags are honoured as _wopen does: _O_TEMPORARY
// deletes the file when its last handle closes (FILE_FLAG_DELETE_ON_CLOSE),
// _O_SHORT_LIVED keeps it in memory where possible (FILE_ATTRIBUTE_TEMPORARY),
// _O_SEQUENTIAL / _O_RANDOM are access hints, _O_NOINHERIT is not inherited,
// and _O_TEXT gives a text-mode descriptor (binary otherwise).
int protopy_open(const char* path, int flags, int mode) {
    DWORD access = 0;
    switch (flags & (_O_RDONLY | _O_WRONLY | _O_RDWR)) {
    case _O_WRONLY: access = GENERIC_WRITE; break;
    case _O_RDWR:   access = GENERIC_READ | GENERIC_WRITE; break;
    default:        access = GENERIC_READ; break;
    }
    DWORD disposition;
    if ((flags & _O_CREAT) && (flags & _O_EXCL)) disposition = CREATE_NEW;
    else if ((flags & _O_CREAT) && (flags & _O_TRUNC)) disposition = CREATE_ALWAYS;
    else if (flags & _O_CREAT) disposition = OPEN_ALWAYS;
    else if (flags & _O_TRUNC) disposition = TRUNCATE_EXISTING;
    else disposition = OPEN_EXISTING;
    // A new file without the owner's write permission is read-only, as _wopen.
    DWORD attributes = ((flags & _O_CREAT) && !(mode & 0200)) ? FILE_ATTRIBUTE_READONLY
                                                              : FILE_ATTRIBUTE_NORMAL;
    if (flags & _O_SHORT_LIVED) attributes = (attributes & ~FILE_ATTRIBUTE_NORMAL) | FILE_ATTRIBUTE_TEMPORARY;
    if (flags & _O_TEMPORARY) {
        attributes |= FILE_FLAG_DELETE_ON_CLOSE;
        access |= DELETE;
    }
    if (flags & _O_SEQUENTIAL) attributes |= FILE_FLAG_SEQUENTIAL_SCAN;
    else if (flags & _O_RANDOM) attributes |= FILE_FLAG_RANDOM_ACCESS;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, (flags & _O_NOINHERIT) ? FALSE : TRUE};
    HANDLE h = CreateFileW(protopy_widen(path).c_str(), access,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa,
                           disposition, attributes, nullptr);
    if (h == INVALID_HANDLE_VALUE) return fail_win32();
    int fd = _open_osfhandle(reinterpret_cast<intptr_t>(h),
                             (flags & (_O_APPEND | _O_RDONLY | _O_WRONLY | _O_RDWR | _O_NOINHERIT))
                                 | ((flags & _O_TEXT) ? _O_TEXT : _O_BINARY));
    if (fd < 0) {
        int err = errno;
        CloseHandle(h);
        errno = err;
    }
    return fd;
}

int protopy_mkdir(const char* path, int /*mode*/) {
    return _wmkdir(protopy_widen(path).c_str());
}

// Removes a file or an empty directory (the link itself, for a symbolic link
// or junction) with POSIX semantics where the file system has them (NTFS,
// Windows 10 1709 and later): the name goes at once even while handles to the
// file are open, as on Linux, so the directory holding it can be removed next.
// A file object protoPython has not closed yet (it closes when collected)
// would otherwise leave the name "delete pending" until that happens.
// Returns 1 when done, 0 when the file system cannot do it (the caller falls
// back to DeleteFileW / RemoveDirectoryW), or -1 with errno set.
static int delete_posix(const std::wstring& w, bool directory) {
    HANDLE h = CreateFileW(w.c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr);
    if (h == INVALID_HANDLE_VALUE) return 0;  // let the classic call report the error
    struct { DWORD Flags; } info = {0x1 /* FILE_DISPOSITION_FLAG_DELETE */ | 0x2 /* POSIX_SEMANTICS */};
    const BOOL ok = SetFileInformationByHandle(h, static_cast<FILE_INFO_BY_HANDLE_CLASS>(21) /* FileDispositionInfoEx */,
                                               &info, sizeof(info));
    const DWORD err = GetLastError();
    CloseHandle(h);
    if (ok) return 1;
    if (err == ERROR_INVALID_PARAMETER || err == ERROR_NOT_SUPPORTED || err == ERROR_INVALID_FUNCTION) return 0;
    errno = protopy_errno_from_win32(err);
    return -1;
}

int protopy_rmdir(const char* path) {
    const std::wstring w = protopy_widen(path);
    const DWORD attr = GetFileAttributesW(w.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        const int r = delete_posix(w, true);
        if (r != 0) return r > 0 ? 0 : -1;
    }
    return _wrmdir(w.c_str());
}

int protopy_unlink(const char* path) {
    std::wstring w = protopy_widen(path);
    // A symbolic link to a directory is removed with RemoveDirectory, as
    // CPython's os.unlink does.
    DWORD attr = GetFileAttributesW(w.c_str());
    const bool dirLink = attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)
                         && (attr & FILE_ATTRIBUTE_REPARSE_POINT);
    // A read-only file stays a PermissionError, as in CPython.
    if (attr != INVALID_FILE_ATTRIBUTES && (dirLink || !(attr & FILE_ATTRIBUTE_DIRECTORY))
        && !(attr & FILE_ATTRIBUTE_READONLY)) {
        const int r = delete_posix(w, dirLink);
        if (r != 0) return r > 0 ? 0 : -1;
    }
    if (dirLink) return RemoveDirectoryW(w.c_str()) ? 0 : fail_win32();
    if (!DeleteFileW(w.c_str())) return fail_win32();
    return 0;
}

int protopy_symlink(const char* target, const char* link, bool directory) {
    const std::wstring wtarget = protopy_widen(target);
    const std::wstring wlink = protopy_widen(link);
    DWORD flags = directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
    // SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE (developer mode, Windows 10
    // 1703+); older systems reject the flag, so retry without it.
    if (CreateSymbolicLinkW(wlink.c_str(), wtarget.c_str(), flags | 0x2)) return 0;
    if (GetLastError() == ERROR_INVALID_PARAMETER && CreateSymbolicLinkW(wlink.c_str(), wtarget.c_str(), flags)) return 0;
    return fail_win32();
}

int protopy_chdir(const char* path) {
    if (!SetCurrentDirectoryW(protopy_widen(path).c_str())) return fail_win32();
    return 0;
}

char* protopy_getcwd(char* buf, size_t size) {
    DWORD n = GetCurrentDirectoryW(0, nullptr);
    if (n == 0) { fail_win32(); return nullptr; }
    std::wstring w(n, L'\0');
    n = GetCurrentDirectoryW(n, w.data());
    w.resize(n);
    std::string s = protopy_narrow(w);
    if (s.size() + 1 > size) { errno = ERANGE; return nullptr; }
    std::memcpy(buf, s.c_str(), s.size() + 1);
    return buf;
}

int protopy_access(const char* path, int mode) {
    // The C runtime rejects X_OK; Windows has no execute permission to check.
    return _waccess(protopy_widen(path).c_str(), mode & (R_OK | W_OK));
}

// Symbolic links and junctions: the substitute name, as CPython's os.readlink.
ssize_t protopy_readlink(const char* path, char* buf, size_t size) {
    std::wstring w = protopy_widen(path);
    HANDLE h = CreateFileW(w.c_str(), FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return fail_win32();
    std::vector<unsigned char> data(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
    DWORD got = 0;
    BOOL ok = DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, nullptr, 0, data.data(),
                              static_cast<DWORD>(data.size()), &got, nullptr);
    DWORD err = GetLastError();
    CloseHandle(h);
    if (!ok) { errno = protopy_errno_from_win32(err); return -1; }
    // REPARSE_DATA_BUFFER (ntifs.h), declared here for user mode.
    struct ReparseHeader {
        ULONG tag; USHORT dataLength; USHORT reserved;
        USHORT substOffset; USHORT substLength; USHORT printOffset; USHORT printLength;
    };
    const ReparseHeader* hdr = reinterpret_cast<const ReparseHeader*>(data.data());
    const unsigned char* pathBuffer;
    if (hdr->tag == IO_REPARSE_TAG_SYMLINK) {
        pathBuffer = data.data() + sizeof(ReparseHeader) + sizeof(ULONG); // + Flags
    } else if (hdr->tag == IO_REPARSE_TAG_MOUNT_POINT) {
        pathBuffer = data.data() + sizeof(ReparseHeader);
    } else {
        errno = EINVAL;
        return -1;
    }
    std::wstring target(reinterpret_cast<const wchar_t*>(pathBuffer + hdr->substOffset),
                        hdr->substLength / sizeof(wchar_t));
    // An NT path ("\??\C:\...") is returned as a Win32 extended path
    // ("\\?\C:\..."), as CPython's os.readlink does.
    if (target.size() > 4 && target.compare(0, 4, L"\\??\\") == 0) target[1] = L'\\';
    std::string s = protopy_narrow(target);
    size_t n = s.size() < size ? s.size() : size;
    std::memcpy(buf, s.data(), n);
    return static_cast<ssize_t>(n);
}

// Descriptor flags: FD_CLOEXEC is "the handle is not inherited".
int protopy_fcntl(int fd, int cmd, int arg) {
    HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    if (h == INVALID_HANDLE_VALUE) { errno = EBADF; return -1; }
    if (cmd == F_GETFD) {
        DWORD flags = 0;
        if (!GetHandleInformation(h, &flags)) return fail_win32();
        return (flags & HANDLE_FLAG_INHERIT) ? 0 : FD_CLOEXEC;
    }
    if (cmd == F_SETFD) {
        DWORD inherit = (arg & FD_CLOEXEC) ? 0 : HANDLE_FLAG_INHERIT;
        if (!SetHandleInformation(h, HANDLE_FLAG_INHERIT, inherit)) return fail_win32();
        return 0;
    }
    errno = EINVAL;
    return -1;
}

int protopy_utimensat(int /*dirfd*/, const char* path, const struct timespec times[2], int /*flags*/) {
    HANDLE h = CreateFileW(protopy_widen(path).c_str(), FILE_WRITE_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return fail_win32();
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    FILETIME ft[2];
    const FILETIME* use[2] = {nullptr, nullptr};
    for (int i = 0; i < 2; ++i) {
        if (times[i].tv_nsec == UTIME_OMIT) continue;
        ft[i] = (times[i].tv_nsec == UTIME_NOW) ? now : timespec_to_filetime(times[i]);
        use[i] = &ft[i];
    }
    BOOL ok = SetFileTime(h, nullptr, use[0], use[1]);
    DWORD err = GetLastError();
    CloseHandle(h);
    if (!ok) { errno = protopy_errno_from_win32(err); return -1; }
    return 0;
}

int protopy_setenv(const char* name, const char* value, int overwrite) {
    std::wstring wname = protopy_widen(name);
    if (!overwrite && _wgetenv(wname.c_str())) return 0;
    return _wputenv_s(wname.c_str(), protopy_widen(value).c_str()) == 0 ? 0 : -1;
}

int protopy_unsetenv(const char* name) {
    // An empty value removes the variable.
    return _wputenv_s(protopy_widen(name).c_str(), L"") == 0 ? 0 : -1;
}

int protopy_pipe(int fds[2]) {
    // Binary and not inherited, as os.pipe() on Windows.
    return _pipe(fds, 65536, _O_BINARY | _O_NOINHERIT);
}

pid_t protopy_getppid() {
    DWORD self = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    pid_t parent = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == self) { parent = static_cast<pid_t>(pe.th32ParentProcessID); break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return parent;
}

int backtrace(void** buffer, int size) {
    if (size <= 0) return 0;
    return static_cast<int>(CaptureStackBackTrace(0, static_cast<DWORD>(size), buffer, nullptr));
}

char** backtrace_symbols(void* const* buffer, int size) {
    // One malloc block, as glibc: the pointer array, then the strings.
    const size_t each = 2 + 2 * sizeof(void*) + 1;
    char** out = static_cast<char**>(std::malloc(static_cast<size_t>(size) * (sizeof(char*) + each)));
    if (!out) return nullptr;
    char* text = reinterpret_cast<char*>(out + size);
    for (int i = 0; i < size; ++i) {
        out[i] = text + static_cast<size_t>(i) * each;
        std::snprintf(out[i], each, "%p", buffer[i]);
    }
    return out;
}

void backtrace_symbols_fd(void* const* buffer, int size, int fd) {
    for (int i = 0; i < size; ++i) {
        char line[2 + 2 * sizeof(void*) + 2];
        int n = std::snprintf(line, sizeof(line), "%p\n", buffer[i]);
        if (n > 0) _write(fd, line, static_cast<unsigned>(n));
    }
}

int protopy_rename(const char* from, const char* to, bool replace) {
    const std::wstring wfrom = protopy_widen(from);
    const std::wstring wto = protopy_widen(to);
    if (replace) {
        // POSIX rename semantics (Windows 10 1607+, NTFS): the destination is
        // replaced even while a file object that has not been collected yet
        // still holds it open. Elsewhere, MoveFileExW below.
        HANDLE h = CreateFileW(wfrom.c_str(), DELETE | SYNCHRONIZE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                               nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            std::wstring full(32768, L'\0');
            DWORD len = GetFullPathNameW(wto.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
            BOOL ok = FALSE;
            if (len > 0 && len < full.size()) {
                full.resize(len);
                std::vector<unsigned char> buf(sizeof(FILE_RENAME_INFO) + full.size() * sizeof(wchar_t));
                auto* info = reinterpret_cast<FILE_RENAME_INFO*>(buf.data());
                info->Flags = 0x1 /* REPLACE_IF_EXISTS */ | 0x2 /* POSIX_SEMANTICS */;
                info->RootDirectory = nullptr;
                info->FileNameLength = static_cast<DWORD>(full.size() * sizeof(wchar_t));
                std::memcpy(info->FileName, full.c_str(), full.size() * sizeof(wchar_t));
                ok = SetFileInformationByHandle(h, static_cast<FILE_INFO_BY_HANDLE_CLASS>(22) /* FileRenameInfoEx */,
                                                info, static_cast<DWORD>(buf.size()));
            }
            DWORD err = GetLastError();
            CloseHandle(h);
            if (ok) return 0;
            if (err != ERROR_INVALID_PARAMETER && err != ERROR_NOT_SUPPORTED
                && err != ERROR_INVALID_FUNCTION) {
                errno = protopy_errno_from_win32(err);
                return -1;
            }
        }
    }
    DWORD flags = replace ? MOVEFILE_REPLACE_EXISTING : 0;
    if (!MoveFileExW(wfrom.c_str(), wto.c_str(), flags))
        return fail_win32();
    return 0;
}

#endif // _WIN32
