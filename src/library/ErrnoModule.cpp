#include <protoPython/ErrnoModule.h>
#include <errno.h>

namespace protoPython {
namespace errno_module {

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(false);
    
    // Most common errno constants
    const char* names[] = {
        "EPERM", "ENOENT", "ESRCH", "EINTR", "EIO", "ENXIO", "E2BIG", "ENOEXEC",
        "EBADF", "ECHILD", "EAGAIN", "ENOMEM", "EACCES", "EFAULT", "ENOTBLK",
        "EBUSY", "EEXIST", "EXDEV", "ENODEV", "ENOTDIR", "EISDIR", "EINVAL",
        "ENFILE", "EMFILE", "ENOTTY", "ETXTBSY", "EFBIG", "ENOSPC", "ESPIPE",
        "EROFS", "EMLINK", "EPIPE", "EDOM", "ERANGE", "EDEADLK", "ENAMETOOLONG",
        "ENOLCK", "ENOSYS", "ENOTEMPTY", "ELOOP", "EWOULDBLOCK", "ENOMSG",
        "EIDRM", "ECHRNG", "EL2NSYNC", "EL3HLT", "EL3RST", "ELNRNG", "EUNATCH",
        "ENOCSI", "EL2HLT", "EBADE", "EBADR", "EXFULL", "ENOANO", "EBADRQC",
        "EBADSLT", "EDEADLOCK", "EBFONT", "ENOSTR", "ENODATA", "ETIME", "ENOSR",
        "ENONET", "ENOPKG", "EREMOTE", "ENOLINK", "EADV", "ESRMNT", "ECOMM",
        "EPROTO", "EMULTIHOP", "EDOTDOT", "EBADMSG", "EOVERFLOW", "ENOTUNIQ",
        "EBADFD", "EREMCHG", "ELIBACC", "ELIBBAD", "ELIBSCN", "ELIBMAX", "ELIBEXEC",
        "EILSEQ", "ERESTART", "ESTRPIPE", "EUSERS", "ENOTSOCK", "EDESTADDRREQ",
        "EMSGSIZE", "EPROTOTYPE", "ENOPROTOOPT", "EPROTONOSUPPORT", "ESOCKTNOSUPPORT",
        "EOPNOTSUPP", "EPFNOSUPPORT", "EAFNOSUPPORT", "EADDRINUSE", "EADDRNOTAVAIL",
        "ENETDOWN", "ENETUNREACH", "ENETRESET", "ECONNABORTED", "ECONNRESET",
        "ENOBUFS", "EISCONN", "ENOTCONN", "ESHUTDOWN", "ETOOMANYREFS", "ETIMEDOUT",
        "ECONNREFUSED", "EHOSTDOWN", "EHOSTUNREACH", "EALREADY", "EINPROGRESS",
        "ESTALE", "EUCLEAN", "ENOTNAM", "ENAVAIL", "EREMOTEIO", "EDQUOT",
        "ENOMEDIUM", "EMEDIUMTYPE", "ECANCELED", "ENOKEY", "EKEYEXPIRED",
        "EKEYREVOKED", "EKEYREJECTED", "EOWNERDEAD", "ENOTRECOVERABLE", "ERFKILL",
        "EHWPOISON"
    };

// Map names to their C errno values
#define ADD_ERRNO(name) \
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, #name), ctx->fromInteger(name))

    ADD_ERRNO(EPERM); ADD_ERRNO(ENOENT); ADD_ERRNO(ESRCH); ADD_ERRNO(EINTR);
    ADD_ERRNO(EIO); ADD_ERRNO(ENXIO); ADD_ERRNO(E2BIG); ADD_ERRNO(ENOEXEC);
    ADD_ERRNO(EBADF); ADD_ERRNO(ECHILD); ADD_ERRNO(EAGAIN); ADD_ERRNO(ENOMEM);
    ADD_ERRNO(EACCES); ADD_ERRNO(EFAULT); 
#ifdef ENOTBLK
    ADD_ERRNO(ENOTBLK);
#endif
    ADD_ERRNO(EBUSY); ADD_ERRNO(EEXIST); ADD_ERRNO(EXDEV); ADD_ERRNO(ENODEV);
    ADD_ERRNO(ENOTDIR); ADD_ERRNO(EISDIR); ADD_ERRNO(EINVAL); ADD_ERRNO(ENFILE);
    ADD_ERRNO(EMFILE); ADD_ERRNO(ENOTTY);
#ifdef ETXTBSY
    ADD_ERRNO(ETXTBSY);
#endif
    ADD_ERRNO(EFBIG); ADD_ERRNO(ENOSPC); ADD_ERRNO(ESPIPE); ADD_ERRNO(EROFS);
    ADD_ERRNO(EMLINK); ADD_ERRNO(EPIPE); ADD_ERRNO(EDOM); ADD_ERRNO(ERANGE);
    ADD_ERRNO(EDEADLK); ADD_ERRNO(ENAMETOOLONG); ADD_ERRNO(ENOLCK); ADD_ERRNO(ENOSYS);
    ADD_ERRNO(ENOTEMPTY); ADD_ERRNO(ELOOP);
#ifdef EWOULDBLOCK
    ADD_ERRNO(EWOULDBLOCK);
#endif
    ADD_ERRNO(ENOMSG); ADD_ERRNO(EIDRM);
#ifdef EILSEQ
    ADD_ERRNO(EILSEQ);
#endif
#ifdef ENOTSOCK
    ADD_ERRNO(ENOTSOCK);
#endif
#ifdef EDESTADDRREQ
    ADD_ERRNO(EDESTADDRREQ);
#endif
#ifdef EMSGSIZE
    ADD_ERRNO(EMSGSIZE);
#endif
#ifdef EPROTOTYPE
    ADD_ERRNO(EPROTOTYPE);
#endif
#ifdef ENOPROTOOPT
    ADD_ERRNO(ENOPROTOOPT);
#endif
#ifdef EPROTONOSUPPORT
    ADD_ERRNO(EPROTONOSUPPORT);
#endif
#ifdef ESOCKTNOSUPPORT
    ADD_ERRNO(ESOCKTNOSUPPORT);
#endif
#ifdef EOPNOTSUPP
    ADD_ERRNO(EOPNOTSUPP);
#endif
#ifdef EPFNOSUPPORT
    ADD_ERRNO(EPFNOSUPPORT);
#endif
#ifdef EAFNOSUPPORT
    ADD_ERRNO(EAFNOSUPPORT);
#endif
#ifdef EADDRINUSE
    ADD_ERRNO(EADDRINUSE);
#endif
#ifdef EADDRNOTAVAIL
    ADD_ERRNO(EADDRNOTAVAIL);
#endif
#ifdef ENETDOWN
    ADD_ERRNO(ENETDOWN);
#endif
#ifdef ENETUNREACH
    ADD_ERRNO(ENETUNREACH);
#endif
#ifdef ENETRESET
    ADD_ERRNO(ENETRESET);
#endif
#ifdef ECONNABORTED
    ADD_ERRNO(ECONNABORTED);
#endif
#ifdef ECONNRESET
    ADD_ERRNO(ECONNRESET);
#endif
#ifdef ENOBUFS
    ADD_ERRNO(ENOBUFS);
#endif
#ifdef EISCONN
    ADD_ERRNO(EISCONN);
#endif
#ifdef ENOTCONN
    ADD_ERRNO(ENOTCONN);
#endif
#ifdef ESHUTDOWN
    ADD_ERRNO(ESHUTDOWN);
#endif
#ifdef ETOOMANYREFS
    ADD_ERRNO(ETOOMANYREFS);
#endif
#ifdef ETIMEDOUT
    ADD_ERRNO(ETIMEDOUT);
#endif
#ifdef ECONNREFUSED
    ADD_ERRNO(ECONNREFUSED);
#endif
#ifdef EHOSTDOWN
    ADD_ERRNO(EHOSTDOWN);
#endif
#ifdef EHOSTUNREACH
    ADD_ERRNO(EHOSTUNREACH);
#endif
#ifdef EALREADY
    ADD_ERRNO(EALREADY);
#endif
#ifdef EINPROGRESS
    ADD_ERRNO(EINPROGRESS);
#endif
#ifdef ESTALE
    ADD_ERRNO(ESTALE);
#endif
#ifdef ENOMEDIUM
    ADD_ERRNO(ENOMEDIUM);
#endif
#ifdef ECANCELED
    ADD_ERRNO(ECANCELED);
#endif
#ifdef EOWNERDEAD
    ADD_ERRNO(EOWNERDEAD);
#endif
#ifdef ENOTRECOVERABLE
    ADD_ERRNO(ENOTRECOVERABLE);
#endif

    // errorcode map
    const proto::ProtoSparseList* errorcode = ctx->newSparseList();
    // For now we don't strictly need the name mapping, just the constants.
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "errorcode"), errorcode->asObject(ctx));

    return mod;
}

} // namespace errno_module
} // namespace protoPython
