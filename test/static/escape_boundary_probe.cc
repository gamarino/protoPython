// Probe for check_escape_boundary.py --probe (not compiled). Every line marked
// "expect" is a way around the escaping boundary the check must report
// (the eight forms of the round-9 review, r9 M2); the other lines are
// accepted forms it must leave alone.
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
