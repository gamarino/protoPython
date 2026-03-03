#include <protoPython/StatModule.h>
#include <sys/stat.h>

namespace protoPython {
namespace stat_module {

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(false);

    // Common stat constants
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "S_IFMT"), ctx->fromInteger(S_IFMT));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "S_IFDIR"), ctx->fromInteger(S_IFDIR));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "S_IFCHR"), ctx->fromInteger(S_IFCHR));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "S_IFBLK"), ctx->fromInteger(S_IFBLK));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "S_IFREG"), ctx->fromInteger(S_IFREG));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "S_IFIFO"), ctx->fromInteger(S_IFIFO));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "S_IFLNK"), ctx->fromInteger(S_IFLNK));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "S_IFSOCK"), ctx->fromInteger(S_IFSOCK));

    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "S_ISUID"), ctx->fromInteger(S_ISUID));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "S_ISGID"), ctx->fromInteger(S_ISGID));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "S_ISVTX"), ctx->fromInteger(S_ISVTX));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "S_IRWXU"), ctx->fromInteger(S_IRWXU));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "S_IRUSR"), ctx->fromInteger(S_IRUSR));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "S_IWUSR"), ctx->fromInteger(S_IWUSR));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "S_IXUSR"), ctx->fromInteger(S_IXUSR));

    // ST_ constants
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ST_MODE"), ctx->fromInteger(0));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ST_INO"), ctx->fromInteger(1));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ST_DEV"), ctx->fromInteger(2));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ST_NLINK"), ctx->fromInteger(3));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ST_UID"), ctx->fromInteger(4));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ST_GID"), ctx->fromInteger(5));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ST_SIZE"), ctx->fromInteger(6));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ST_ATIME"), ctx->fromInteger(7));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ST_MTIME"), ctx->fromInteger(8));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ST_CTIME"), ctx->fromInteger(9));

    return mod;
}

} // namespace stat_module
} // namespace protoPython
