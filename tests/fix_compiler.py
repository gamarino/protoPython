import sys

file_path = 'src/library/Compiler.cpp'
with open(file_path, 'r') as f:
    content = f.read()

# 1. compileFunctionDef fix
old_func = """    Compiler bodyCompiler(ctx_, filename_);
    bodyCompiler.globalNames_ = bodyGlobals;
    bodyCompiler.nonlocalNames_ = bodyNonlocals;
    bodyCompiler.localSlotMap_ = slotMap;"""

new_func = """    Compiler bodyCompiler(ctx_, filename_);
    bodyCompiler.globalNames_ = globalNames_;
    for (const auto& g : bodyGlobals) bodyCompiler.globalNames_.insert(g);
    bodyCompiler.nonlocalNames_ = bodyNonlocals;
    bodyCompiler.localSlotMap_ = slotMap;"""

content = content.replace(old_func, new_func)

# 2. compileAsyncFunctionDef fix
# Note: It might have the exact same string, so replace(..., ..., 1) or similar isn't needed if they are identical.
# Actually, let's be more specific for AsyncFunctionDef if it didn't catch it.
# Based on my view_file, it IS identical.

# 3. compileDictComp fix (around 779)
old_dict = """    Compiler bodyCompiler(ctx_, filename_);
    bodyCompiler.localSlotMap_[".0"] = 0;
    
    // Collect locals and nonlocals for comprehension scope"""
new_dict = """    Compiler bodyCompiler(ctx_, filename_);
    bodyCompiler.localSlotMap_[".0"] = 0;
    bodyCompiler.globalNames_ = globalNames_;
    
    // Collect locals and nonlocals for comprehension scope"""

content = content.replace(old_dict, new_dict)

# 4. compileSetComp fix (around 852)
old_set = """    Compiler bodyCompiler(ctx_, filename_);
    bodyCompiler.localSlotMap_[".0"] = 0;
    
    // Collect locals and nonlocals for comprehension scope"""
# Note: same as DictComp!

# 5. compileClassDef fix (around 2276)
old_class = """    // 3. Body
    Compiler bodyCompiler(ctx_, filename_);
    if (!bodyCompiler.compileNode(n->body.get())) return false;"""

new_class = """    // 3. Body
    Compiler bodyCompiler(ctx_, filename_);
    bodyCompiler.globalNames_ = globalNames_;
    bodyCompiler.nonlocalNames_ = nonlocalNames_;
    if (!bodyCompiler.compileNode(n->body.get())) return false;"""

content = content.replace(old_class, new_class)

with open(file_path, 'w') as f:
    f.write(content)

print("Successfully updated Compiler.cpp")
