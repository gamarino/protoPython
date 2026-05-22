import test_descr
cpm = test_descr.ClassPropertiesAndMethods()
mro = test_descr.MroTest()
misc = test_descr.MiscTests()
all_groups = [cpm, mro, misc]

candidates = [
    'test_altmro', 'test_mutable_bases', 'test_subclass_propagation',
    'test_remove_subclass', 'test_set_class', 'test_dir',
    'test_classmethod_staticmethod_annotations', 'test_metaclass',
    'test_disappearing_custom_mro', 'test_descrdoc',
    'test_special_method_lookup', 'test_type_lookup_mro_reference',
    'test_mutable_bases_catch_mro_conflict', 'test_funny_new',
    'test_errors', 'test_gh55664', 'test_buffer_inheritance',
    'test_builtin_bases', 'test_delete_hook', 'test_proxy_call',
    'test_uninitialized_modules', 'test_wrong_class_slot_wrapper',
    'test_vicious_descriptor_nonsense', 'test_carloverre_multi_inherit_invalid',
    'test_reent_set_bases_tp_base_cycle',
]
for name in candidates:
    found = None
    for g in all_groups:
        m = getattr(g, name, None)
        if m is not None:
            found = (g, m)
            break
    if found is None:
        print(f"{name}: NOT FOUND")
        continue
    grp, m = found
    try:
        if hasattr(grp, 'setUp'):
            grp.setUp()
        m()
        print(f"PASS: {name}")
    except Exception as e:
        msg = str(e)
        if len(msg) > 100: msg = msg[:100] + "..."
        print(f"FAIL {name}: {type(e).__name__}: {msg}")
