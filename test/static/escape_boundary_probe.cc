// Probe for check_escape_boundary.py --probe (not compiled). Every line marked
// "expect" is a way around the escaping boundary the check must report
// (the eight forms of the round-9 review, r9 M2, and the nineteen of the
// round-10 review, r10 M1); the other lines are accepted forms it must
// leave alone.
static void probe(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const proto::ProtoObject* nameObj,
                  const proto::ProtoList* args, const std::string& attrName, const char* cName) {
    obj->getAttribute(ctx, nameObj->asString(ctx));                       // expect: canonical pattern
    const proto::ProtoString* k = nameObj->asString(ctx);
    obj->getAttribute(ctx, k);                                            // expect: via a local
    obj->getAttribute(ctx, args->getAt(ctx, 0)->asString(ctx));          // expect: getAt(ctx, 0)
    obj->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, attrName));  // expect: round-8 M2-a
    obj->setAttribute(ctx,                                                // expect: split over two lines
                      nameObj->asString(ctx), PROTO_NONE);
    std::string esc = std::string("\x01") + attrName;                    // expect: re-implemented prefix
    bool internal = attrName.size() > 1 && attrName.at(1) == '\x01';     // expect: re-implemented test
    obj->hasOwnAttribute(ctx, proto::ProtoString::createSymbol(ctx, attrName.c_str()));  // expect: createSymbol
    obj->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, attrName)->asString(ctx), PROTO_NONE);  // expect
    obj->getAttribute(ctx, PythonEnvironment::pythonAttributeKey(ctx, attrName));
    obj->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__name__"));
    obj->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, cName));
    obj->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "x"),
                      PythonEnvironment::getInternedString(ctx, attrName)->asObject(ctx));
    obj->getAttribute(ctx, nameObj->asString(ctx));  // raw-key: a reason given
}

// A helper that returns a raw key (round 10, form 19).
static const proto::ProtoString* keyFor(proto::ProtoContext* ctx, const std::string& n) {
    return ctx->fromUTF8String(n.c_str())->asString(ctx);
}

static void probe2(proto::ProtoContext* ctx, const proto::ProtoObject* obj, const proto::ProtoObject* nameObj,
                   const std::string& attrName, bool cond) {
    const char* nm = attrName.c_str();
    obj->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, nm));             // expect: 9 const char* from c_str()
    auto toKey = [&](const proto::ProtoObject* o) { return o->asString(ctx); };
    obj->getAttribute(ctx, toKey(nameObj));                                            // expect: 10 via a lambda
    auto s1 = nameObj->asString(ctx);
    auto k2 = s1;
    obj->getAttribute(ctx, k2);                                                        // expect: 11 two-level local
    std::string pre(1, '\x01');                                                        // expect: 12 prefix char
    bool i1 = !attrName.empty() && attrName[0] == 1;                                   // expect: 13 integer 1
    bool i2 = !attrName.empty() && attrName[0] == '\1';                                // expect: 14 octal short form
    bool i3 = attrName.rfind("\u0001", 0) == 0;                                        // expect: 15 \u0001
    std::string pre2 = "\1";                                                           // expect: 16 "\1"
    bool i4 = attrName.compare(0, 1, "\x01") == 0;                                     // expect: 17 compare()
    obj->getAttribute(ctx, ctx->fromUTF8String(attrName.c_str())->asString(ctx));      // expect: 18 fromUTF8String
    obj->getAttribute(ctx, keyFor(ctx, attrName));                                     // expect: 19 helper returning a raw key
    const proto::ProtoString* k3;
    k3 = nameObj->asString(ctx);
    obj->getAttribute(ctx, k3);                                                        // expect: 20 assignment without declaration
    obj->getAttribute(ctx, (nameObj)->asString(ctx));                                  // expect: 21 parenthesised
    obj->getAttribute(ctx, static_cast<const proto::ProtoString*>(nameObj->asString(ctx)));  // expect: 22 cast
    proto::ProtoObject::getAttribute(obj, ctx, nameObj->asString(ctx));                // expect: 23 qualified call
    auto g = &proto::ProtoObject::getAttribute;
    (obj->*g)(ctx, nameObj->asString(ctx));                                            // expect: 24 member pointer
    obj->getAttribute(ctx, cond ? getInternedString(ctx, "x") : nameObj->asString(ctx));  // expect: 25 ?: branch
    for (int i = 0; i < 1; ++i) obj->getAttribute(ctx, nameObj->asString(ctx));        // expect: 26 for statement
    if (cond) return; else obj->getAttribute(ctx, nameObj->asString(ctx));             // expect: 27 else branch
    // Accepted: a const char* from a literal, a cast of an escaped key, an
    // integer compared with an element that is not a name.
    const char* lit = "__name__";
    obj->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, lit));
    obj->getAttribute(ctx, static_cast<const proto::ProtoString*>(PythonEnvironment::pythonAttributeKey(ctx, attrName)));
    bool one = attrName.size() == 1;
}
