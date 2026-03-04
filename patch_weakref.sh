sed -i 's/mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ref"), refType);/&\n        fprintf(stderr, "DEBUG: _weakref setAttribute(ref)\\n"); /' src/library/WeakrefModule.cpp
