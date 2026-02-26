def patch_collections():
    with open("lib/python3.14/collections/__init__.py", "r", encoding="utf-8") as f:
        lines = f.readlines()

    start_idx = -1
    for i, line in enumerate(lines):
        if line.startswith("def namedtuple("):
            start_idx = i
            break
            
    if start_idx == -1:
        print("COULD NOT FIND START")
        return
        
    end_idx = -1
    for i in range(start_idx, len(lines)):
        if "return result" in lines[i] and lines[i].startswith("    return"):
            end_idx = i
            break
            
    if end_idx == -1:
        print("COULD NOT FIND END")
        return
        
    before = lines[:start_idx]
    after = lines[end_idx+1:]
    
    new_func = """def namedtuple(typename, field_names, *, rename=False, defaults=None, module=None):
    print("DEBUG: TRACE A: start")
    try:
        if isinstance(field_names, str):
            field_names = field_names.replace(',', ' ').split()
        print("DEBUG: TRACE B: map start")
        field_names = list(map(str, field_names))
        print("DEBUG: TRACE C: intern")
        typename = _sys.intern(str(typename))
        print("DEBUG: TRACE D: rename start")

        if rename:
            seen = set()
            for index, name in enumerate(field_names):
                if (not name.isidentifier()
                    or _iskeyword(name)
                    or name.startswith('_')
                    or name in seen):
                    field_names[index] = f'_{index}'
                seen.add(name)

        print("DEBUG: TRACE E: loops")
        for name in [typename] + field_names:
            if type(name) is not str:
                raise TypeError('...')
            if not name.isidentifier():
                raise ValueError('...')
            if _iskeyword(name):
                raise ValueError('...')

        print("DEBUG: TRACE F: seen")
        seen = set()
        for name in field_names:
            if name.startswith('_') and not rename:
                raise ValueError('...')
            if name in seen:
                raise ValueError('...')
            seen.add(name)

        print("DEBUG: TRACE G: defaults")
        field_defaults = {}
        if defaults is not None:
            defaults = tuple(defaults)
            if len(defaults) > len(field_names):
                raise TypeError('...')
            field_defaults = dict(reversed(list(zip(reversed(field_names), reversed(defaults)))))

        print("DEBUG: TRACE H: field_names intern")
        field_names = tuple(map(_sys.intern, field_names))
        num_fields = len(field_names)
        arg_list = ', '.join(field_names)
        if num_fields == 1:
            arg_list += ','
        
        print("DEBUG: TRACE I: repr_fmt")
        repr_fmt = '(' + ', '.join(f'{name}=%r' for name in field_names) + ')'
        tuple_new = tuple.__new__
        _dict, _tuple, _len, _map, _zip = dict, tuple, len, map, zip

        print("DEBUG: TRACE J: __new__ source")
        s = f"def __new__(_cls, {arg_list}): return _tuple_new(_cls, ({arg_list}))"
        namespace = {'_tuple_new': tuple_new, '__name__': f'namedtuple_{typename}'}
        
        print("DEBUG: TRACE K: exec")
        exec(s, namespace)
        print("DEBUG: TRACE L: after exec")
        __new__ = namespace['__new__']
        __new__.__doc__ = f'Create new instance of {typename}({arg_list})'
        if defaults is not None:
            __new__.__defaults__ = defaults

        print("DEBUG: TRACE M: class namespace")
        class_namespace = {
            '__doc__': f'{typename}({arg_list})',
            '__slots__': (),
            '_fields': field_names,
            '_field_defaults': field_defaults,
            '__new__': __new__.__func__,
        }
        
        print("DEBUG: TRACE N: calling type()")
        result = type(typename, (tuple,), class_namespace)
        
        print("DEBUG: TRACE O: module resolution")
        if module is None:
            try:
                frame = _sys._getframe(1)
                module = frame.f_globals.get('__name__', '__main__') if frame is not None else '__main__'
            except (AttributeError, ValueError):
                pass
        if module is not None:
            result.__module__ = module

        print("DEBUG: TRACE SUMMARY: ALL SUCCEEDED")
        return result
    except Exception as e:
        print("DEBUG: EXCEPTION INSIDE NAMEDTUPLE:", repr(e))
        raise

"""
    
    with open("lib/python3.14/collections/__init__.py", "w", encoding="utf-8") as f:
        f.writelines(before)
        f.write(new_func)
        f.writelines(after)
    
    print("PATCH APPLIED MY WAY AGAIN")

if __name__ == "__main__":
    patch_collections()
