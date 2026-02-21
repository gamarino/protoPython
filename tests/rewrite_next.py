import re

with open('src/library/PythonEnvironment.cpp', 'r') as f:
    content = f.read()

def replacer(match):
    var_decl = match.group(1)
    iter_obj = match.group(2)
    arg_list = match.group(3)
    return f"{var_decl} = PythonEnvironment::fromContext(context) ? PythonEnvironment::fromContext(context)->next({iter_obj}) : nextM->asMethod(context)(context, {iter_obj}, nullptr, {arg_list}, nullptr);"

content = re.sub(
    r'(const proto::ProtoObject\*\s+\b\w+\b)\s*=\s*nextM->asMethod\(context\)\(context,\s*(it|it2|itObj),\s*nullptr,\s*(emptyL|context->newList\(\)),\s*nullptr\);',
    replacer,
    content
)

with open('src/library/PythonEnvironment.cpp', 'w') as f:
    f.write(content)
