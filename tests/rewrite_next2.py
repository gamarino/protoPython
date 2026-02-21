import re

with open('src/library/PythonEnvironment.cpp', 'r') as f:
    content = f.read()

def replacer_nextAttr(match):
    var_decl = match.group(1)
    iter_obj = match.group(2)
    arg_list = match.group(3)
    return f"{var_decl} = PythonEnvironment::fromContext(context) ? PythonEnvironment::fromContext(context)->next({iter_obj}) : nextAttr->asMethod(context)(context, {iter_obj}, nullptr, {arg_list}, nullptr);"

content = re.sub(
    r'(const proto::ProtoObject\*\s+\b\w+\b)\s*=\s*nextAttr->asMethod\(context\)\(context,\s*(iterResult),\s*nullptr,\s*(nextArgs),\s*nullptr\);',
    replacer_nextAttr,
    content
)

def replacer_nextFn(match):
    var_decl = match.group(1)
    iter_obj = match.group(2)
    arg_list = match.group(3)
    return f"{var_decl} = PythonEnvironment::fromContext(context) ? PythonEnvironment::fromContext(context)->next({iter_obj}) : nextFn(context, {iter_obj}, nullptr, {arg_list}, nullptr);"

content = re.sub(
    r'(const proto::ProtoObject\*\s+\b\w+\b)\s*=\s*nextFn\(context,\s*(it),\s*nullptr,\s*(context->newList\(\)),\s*nullptr\);',
    replacer_nextFn,
    content
)

with open('src/library/PythonEnvironment.cpp', 'w') as f:
    f.write(content)
