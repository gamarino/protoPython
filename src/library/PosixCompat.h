// PosixCompat.h — the POSIX calls protoPython's native modules make, on Windows.
//
// On Linux and macOS this header is empty: the modules include their POSIX
// headers themselves and compile to exactly what they were.
//
// On Windows (MSVC) it maps the same names onto the C runtime and Win32, so a
// module keeps one body for every platform:
//   - stat/lstat/fstat fill a `struct stat` with the fields POSIX code reads
//     (st_atim/st_mtim/st_ctim with nanoseconds, st_ino from the file index,
//     st_blksize, st_blocks), the way CPython's os.stat does on Windows;
//   - opendir/readdir/closedir over FindFirstFileW;
//   - open/mkdir/rmdir/unlink/chdir/getcwd/access/setenv/unsetenv take UTF-8
//     paths and call the UTF-16 functions; files are always opened binary;
//   - the POSIX mode bits and S_IS* predicates, with the POSIX values (the ones
//     CPython's stat module uses on every platform).
//
// Include it AFTER every other header: it defines `stat`, `lstat`, `fstat`,
// `open`, `mkdir`, ... as macros on Windows.
#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <cstdint>
#include <string>

// --- Types -------------------------------------------------------------------
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
typedef int pid_t;

// --- File type and permission bits (POSIX values) -----------------------------
#ifndef S_IFMT
#define S_IFMT   0170000
#endif
#ifndef S_IFSOCK
#define S_IFSOCK 0140000
#endif
#ifndef S_IFLNK
#define S_IFLNK  0120000
#endif
#ifndef S_IFREG
#define S_IFREG  0100000
#endif
#ifndef S_IFBLK
#define S_IFBLK  0060000
#endif
#ifndef S_IFDIR
#define S_IFDIR  0040000
#endif
#ifndef S_IFCHR
#define S_IFCHR  0020000
#endif
#ifndef S_IFIFO
#define S_IFIFO  0010000
#endif
#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000
#define S_IRWXU 00700
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100
#define S_IRWXG 00070
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010
#define S_IRWXO 00007
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

// --- access() modes, standard descriptors, open() flags ------------------------
#ifndef F_OK
#define F_OK 0
#endif
#ifndef X_OK
#define X_OK 1
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef R_OK
#define R_OK 4
#endif
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif
// Not inherited by child processes: what O_CLOEXEC means on POSIX.
#ifndef O_CLOEXEC
#define O_CLOEXEC _O_NOINHERIT
#endif

// --- errno values the C runtime lacks: Winsock's codes, as CPython's errno ------
#ifndef ESHUTDOWN
#define ESHUTDOWN 10058     // WSAESHUTDOWN
#endif
#ifndef ETOOMANYREFS
#define ETOOMANYREFS 10059  // WSAETOOMANYREFS
#endif
#ifndef EHOSTDOWN
#define EHOSTDOWN 10064     // WSAEHOSTDOWN
#endif

// --- fcntl() descriptor flags (inheritance only) --------------------------------
#define F_GETFD 1
#define F_SETFD 2
#define FD_CLOEXEC 1

// --- utimensat() -------------------------------------------------------------------
#define AT_FDCWD (-100)
#define UTIME_NOW  ((1l << 30) - 1l)
#define UTIME_OMIT ((1l << 30) - 2l)

// --- stat ----------------------------------------------------------------------------
struct protopy_stat {
    unsigned int st_mode;
    unsigned long long st_ino;
    unsigned long long st_dev;
    unsigned long long st_nlink;
    unsigned int st_uid;
    unsigned int st_gid;
    long long st_size;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
    long long st_blksize;
    long long st_blocks;
    unsigned long long st_rdev;
};
int protopy_stat(const char* path, struct protopy_stat* st);
int protopy_lstat(const char* path, struct protopy_stat* st);
int protopy_fstat(int fd, struct protopy_stat* st);

// --- Directories ---------------------------------------------------------------------
struct dirent {
    char d_name[1024];
};
struct DIR;
DIR* opendir(const char* path);
struct dirent* readdir(DIR* dir);
int closedir(DIR* dir);

// --- Paths and files (UTF-8 in, UTF-16 calls, binary files) --------------------------
int protopy_open(const char* path, int flags, int mode = 0);
int protopy_mkdir(const char* path, int mode);
int protopy_rmdir(const char* path);
int protopy_unlink(const char* path);
int protopy_chdir(const char* path);
char* protopy_getcwd(char* buf, size_t size);
int protopy_access(const char* path, int mode);
ssize_t protopy_readlink(const char* path, char* buf, size_t size);
int protopy_fcntl(int fd, int cmd, int arg = 0);
int protopy_utimensat(int dirfd, const char* path, const struct timespec times[2], int flags);
int protopy_setenv(const char* name, const char* value, int overwrite);
int protopy_unsetenv(const char* name);
int protopy_pipe(int fds[2]);
pid_t protopy_getppid();

// Renames `from` to `to`; replaces an existing `to` only when `replace` is set
// (os.rename refuses, os.replace replaces, as CPython on Windows).
int protopy_rename(const char* from, const char* to, bool replace);
// GetLastError() → errno, as CPython's winerror_to_errno.
int protopy_errno_from_win32(unsigned long winerror);
// UTF-8 ↔ UTF-16.
std::wstring protopy_widen(const std::string& utf8);
std::string protopy_narrow(const std::wstring& utf16);
// The running executable, as UTF-8 (GetModuleFileNameW).
std::string protopy_executable_path();
// <execinfo.h>: return addresses from CaptureStackBackTrace; the symbols are
// the addresses in hexadecimal.
int backtrace(void** buffer, int size);
char** backtrace_symbols(void* const* buffer, int size);
void backtrace_symbols_fd(void* const* buffer, int size, int fd);

// `struct stat` and stat() both become protopy_stat, as POSIX names both.
#define stat protopy_stat
#define lstat protopy_lstat
#define fstat protopy_fstat
#define open protopy_open
#define mkdir(p, m) protopy_mkdir(p, m)
#define rmdir protopy_rmdir
#define unlink protopy_unlink
#define chdir protopy_chdir
#define getcwd protopy_getcwd
#define access protopy_access
#define readlink protopy_readlink
#define fcntl protopy_fcntl
#define utimensat protopy_utimensat
#define setenv protopy_setenv
#define unsetenv protopy_unsetenv
#define pipe protopy_pipe
#define getppid protopy_getppid

#endif // _WIN32
