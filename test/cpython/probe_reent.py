import test_descr
mro = test_descr.MroTest()
for name in ['test_reent_set_bases_on_base', 'test_tp_subclasses_cycle_in_update_slots',
             'test_reent_set_bases_tp_base_cycle']:
    try:
        m = getattr(mro, name, None)
        if m is None:
            print(f"{name}: NOT FOUND")
            continue
        m()
        print(f"PASS: {name}")
    except Exception as e:
        msg = str(e)
        if len(msg) > 100: msg = msg[:100]
        print(f"FAIL {name}: {type(e).__name__}: {msg}")
