import sys

file_path = 'src/library/Compiler.cpp'
with open(file_path, 'r') as f:
    content = f.read()

# 1. compileFunctionDef fix
# Locate line 1871
old_line = "    for (const auto& c : captured) bodyNonlocals.insert(c);"
# We need to move this after varnamesOrdered is built, or just check against params/localsOrdered.
# Let's replace the capture logic in compileFunctionDef.

old_block_func = """    std::unordered_set<std::string> captured;
    collectCapturedNames(n->body.get(), combinedGlobals, captured);
    for (const auto& c : captured) bodyNonlocals.insert(c);

    std::string dynamicReason = getDynamicLocalsReason(n->body.get());"""

new_block_func = """    std::unordered_set<std::string> captured;
    collectCapturedNames(n->body.get(), combinedGlobals, captured);
    // Captured names that are NOT defined in this function are its nonlocals.
    // Captured names that ARE defined here just trigger forceMapped.
    for (const auto& c : captured) {
        bool isLocal = false;
        for (const auto& p : params) if (p == c) isLocal = true;
        for (const auto& l : localsOrdered) if (l == c) isLocal = true;
        if (!isLocal) bodyNonlocals.insert(c);
    }

    std::string dynamicReason = getDynamicLocalsReason(n->body.get());"""

content = content.replace(old_block_func, new_block_func)

# 2. compileLambda fix
old_block_lambda = """    std::unordered_set<std::string> bodyNonlocals;
    for (const auto& c : captured) bodyNonlocals.insert(c);

    const bool forceMapped = !captured.empty();"""

new_block_lambda = """    std::unordered_set<std::string> bodyNonlocals;
    for (const auto& c : captured) {
        bool isParam = false;
        for (const auto& p : params) if (p == c) isParam = true;
        if (!isParam) bodyNonlocals.insert(c);
    }

    const bool forceMapped = !captured.empty();"""

content = content.replace(old_block_lambda, new_block_lambda)

# 3. compileAsyncFunctionDef fix
# Same as compileFunctionDef but uses n->parameters and localsOrdered.
old_block_async = """    std::unordered_set<std::string> captured;
    collectCapturedNames(n->body.get(), combinedGlobals, captured);
    for (const auto& c : captured) bodyNonlocals.insert(c);
    
    std::string dynamicReason = getDynamicLocalsReason(n->body.get());"""

new_block_async = """    std::unordered_set<std::string> captured;
    collectCapturedNames(n->body.get(), combinedGlobals, captured);
    for (const auto& c : captured) {
        bool isLocal = false;
        for (const auto& p : params) if (p == c) isLocal = true;
        for (const auto& l : localsOrdered) if (l == c) isLocal = true;
        if (!isLocal) bodyNonlocals.insert(c);
    }
    
    std::string dynamicReason = getDynamicLocalsReason(n->body.get());"""

content = content.replace(old_block_async, new_block_async)

# 4. collectUsedNames recursion fix
old_used = """    if (auto* fn = dynamic_cast<FunctionDefNode*>(node)) {
        out.insert(fn->name);
        // Note: parameters are handled separately in compileFunctionDef, 
        // but adding them here doesn't hurt if we want a complete set of locals.
        for (const auto& p : fn->parameters) out.insert(p);
        // We do NOT recurse into fn->body because those are local to the NESTED function.
        return;
    }"""

new_used = """    if (auto* fn = dynamic_cast<FunctionDefNode*>(node)) {
        out.insert(fn->name);
        for (const auto& p : fn->parameters) out.insert(p);
        // Recurse to find captures for outer scopes
        collectUsedNames(fn->body.get(), out);
        return;
    }"""

content = content.replace(old_used, new_used)

# 5. Fix for ClassDefNode in collectUsedNames too
old_used_class = """    if (auto* cl = dynamic_cast<ClassDefNode*>(node)) {
        out.insert(cl->name);
        // We do NOT recurse into cl->body.
        return;
    }"""
new_used_class = """    if (auto* cl = dynamic_cast<ClassDefNode*>(node)) {
        out.insert(cl->name);
        collectUsedNames(cl->body.get(), out);
        return;
    }"""
content = content.replace(old_used_class, new_used_class)

with open(file_path, 'w') as f:
    f.write(content)

print("Successfully updated Compiler.cpp with refined scoping and recursive used name collection")
