import test_descr, traceback
t = test_descr.ClassPropertiesAndMethods()
try: t.test_metaclass()
except Exception as e:
    traceback.print_exc()
