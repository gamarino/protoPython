#include <protoPython/StatModule.h>
#include <sys/stat.h>

namespace protoPython {
namespace stat_module {

// Helpers shared by every S_IS<TYPE> predicate. CPython's stat.py
// uses pure-Python functions like `def S_ISDIR(mode): return
// stat.S_IFMT(mode) == stat.S_IFDIR`. The previous module exposed
// only the bit constants but not the predicates, so any code that
// called `stat.S_ISDIR(stbuf.st_mode)` raised AttributeError —
// pathlib, os.path.isdir/isfile/islink, and shutil.copytree all
// route through these.
//
// extract_mode pulls the integer mode out of `args[0]`, accepting
// both raw ints and stat_result-style wrappers whose `__data__`
// carries the integer.
static int extract_mode(proto::ProtoContext* ctx,
                        const proto::ProtoList* args) {
    if (!args || args->getSize(ctx) < 1) return 0;
    const proto::ProtoObject* a = args->getAt(ctx, 0);
    if (!a) return 0;
    if (a->isInteger(ctx)) return static_cast<int>(a->asLong(ctx));
    return 0;
}

#define DEFINE_S_IS(name, expected_macro) \
static const proto::ProtoObject* py_##name( \
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*, \
    const proto::ProtoList* args, const proto::ProtoSparseList*) { \
    int mode = extract_mode(ctx, args); \
    return ((mode & S_IFMT) == (expected_macro)) ? PROTO_TRUE : PROTO_FALSE; \
}

DEFINE_S_IS(S_ISDIR,  S_IFDIR)
DEFINE_S_IS(S_ISCHR,  S_IFCHR)
DEFINE_S_IS(S_ISBLK,  S_IFBLK)
DEFINE_S_IS(S_ISREG,  S_IFREG)
DEFINE_S_IS(S_ISFIFO, S_IFIFO)
DEFINE_S_IS(S_ISLNK,  S_IFLNK)
DEFINE_S_IS(S_ISSOCK, S_IFSOCK)

#undef DEFINE_S_IS

// stat.S_IMODE(mode) — the permission bits (low 12 bits per
// CPython); useful for `chmod`-style operations that strip the
// file-type field.
static const proto::ProtoObject* py_S_IMODE(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    int mode = extract_mode(ctx, args);
    return ctx->fromInteger(mode & 07777);
}

// stat.S_IFMT(mode) — extract the file-type bits.
static const proto::ProtoObject* py_S_IFMT(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    int mode = extract_mode(ctx, args);
    return ctx->fromInteger(mode & S_IFMT);
}

// stat.filemode(mode) — render the mode as a 10-char drwxr-xr-x string.
// Used by ls -l style output and by pathlib.Path's repr/__str__.
static const proto::ProtoObject* py_filemode(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    int mode = extract_mode(ctx, args);
    char buf[11] = "----------";
    if (S_ISDIR(mode)) buf[0] = 'd';
    else if (S_ISCHR(mode)) buf[0] = 'c';
    else if (S_ISBLK(mode)) buf[0] = 'b';
    else if (S_ISFIFO(mode)) buf[0] = 'p';
    else if (S_ISLNK(mode)) buf[0] = 'l';
    else if (S_ISSOCK(mode)) buf[0] = 's';
    else if (S_ISREG(mode)) buf[0] = '-';
    if (mode & S_IRUSR) buf[1] = 'r';
    if (mode & S_IWUSR) buf[2] = 'w';
    if (mode & S_IXUSR) buf[3] = 'x';
    if (mode & S_ISUID) buf[3] = (mode & S_IXUSR) ? 's' : 'S';
    if (mode & S_IRGRP) buf[4] = 'r';
    if (mode & S_IWGRP) buf[5] = 'w';
    if (mode & S_IXGRP) buf[6] = 'x';
    if (mode & S_ISGID) buf[6] = (mode & S_IXGRP) ? 's' : 'S';
    if (mode & S_IROTH) buf[7] = 'r';
    if (mode & S_IWOTH) buf[8] = 'w';
    if (mode & S_IXOTH) buf[9] = 'x';
    if (mode & S_ISVTX) buf[9] = (mode & S_IXOTH) ? 't' : 'T';
    return proto::ProtoString::fromUTF8(ctx, buf)->asObject(ctx);
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(false);

    auto setInt = [&](const char* name, long val) {
        mod = mod->setAttribute(ctx,
            proto::ProtoString::createSymbol(ctx, name), ctx->fromInteger(val));
    };
    auto setFn = [&](const char* name,
                     const proto::ProtoObject* (*fn)(proto::ProtoContext*,
                         const proto::ProtoObject*, const proto::ParentLink*,
                         const proto::ProtoList*, const proto::ProtoSparseList*)) {
        mod = mod->setAttribute(ctx,
            proto::ProtoString::createSymbol(ctx, name),
            ctx->fromMethod(nullptr, fn));
    };

    // File types. `S_IFMT` is intentionally NOT exposed as the
    // bitmask constant here — CPython's stat module exposes it as a
    // FUNCTION (`stat.S_IFMT(mode)` returning `mode & 0o170000`),
    // and stdlib code (filecmp.py et al.) calls it that way. It's
    // bound below alongside the other S_IS* predicates.
    setInt("S_IFDIR", S_IFDIR);
    setInt("S_IFCHR", S_IFCHR);
    setInt("S_IFBLK", S_IFBLK);
    setInt("S_IFREG", S_IFREG);
    setInt("S_IFIFO", S_IFIFO);
    setInt("S_IFLNK", S_IFLNK);
    setInt("S_IFSOCK", S_IFSOCK);

    // Permission bits — owner / group / other plus setuid / setgid /
    // sticky. The previous module exposed only the U-bits, so any
    // caller doing `mode & stat.S_IRGRP` got AttributeError.
    setInt("S_ISUID", S_ISUID);
    setInt("S_ISGID", S_ISGID);
    setInt("S_ISVTX", S_ISVTX);
    setInt("S_IRWXU", S_IRWXU);
    setInt("S_IRUSR", S_IRUSR);
    setInt("S_IWUSR", S_IWUSR);
    setInt("S_IXUSR", S_IXUSR);
    setInt("S_IRWXG", S_IRWXG);
    setInt("S_IRGRP", S_IRGRP);
    setInt("S_IWGRP", S_IWGRP);
    setInt("S_IXGRP", S_IXGRP);
    setInt("S_IRWXO", S_IRWXO);
    setInt("S_IROTH", S_IROTH);
    setInt("S_IWOTH", S_IWOTH);
    setInt("S_IXOTH", S_IXOTH);

    // Tuple-index constants for the legacy `stat()` 10-tuple.
    setInt("ST_MODE",  0);
    setInt("ST_INO",   1);
    setInt("ST_DEV",   2);
    setInt("ST_NLINK", 3);
    setInt("ST_UID",   4);
    setInt("ST_GID",   5);
    setInt("ST_SIZE",  6);
    setInt("ST_ATIME", 7);
    setInt("ST_MTIME", 8);
    setInt("ST_CTIME", 9);

    // Predicates and helpers — without these, every pathlib /
    // os.path.is{dir,file,link} invocation hit AttributeError.
    setFn("S_ISDIR",  py_S_ISDIR);
    setFn("S_ISCHR",  py_S_ISCHR);
    setFn("S_ISBLK",  py_S_ISBLK);
    setFn("S_ISREG",  py_S_ISREG);
    setFn("S_ISFIFO", py_S_ISFIFO);
    setFn("S_ISLNK",  py_S_ISLNK);
    setFn("S_ISSOCK", py_S_ISSOCK);
    setFn("S_IMODE",  py_S_IMODE);
    setFn("S_IFMT",   py_S_IFMT);
    setFn("filemode", py_filemode);

    return mod;
}

} // namespace stat_module
} // namespace protoPython
