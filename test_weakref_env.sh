sed -i 's/if (env) {/&\n        fprintf(stderr, "DEBUG: _weakref inside if(env)\\n"); /' src/library/WeakrefModule.cpp
sed -i 's/PythonEnvironment\* env = PythonEnvironment::fromContext(ctx);/&\n    fprintf(stderr, "DEBUG: _weakref env=%p\\n", (void*)env); /' src/library/WeakrefModule.cpp
